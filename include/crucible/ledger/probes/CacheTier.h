#pragma once

// #69 — where the cache-tier rule's thresholds actually sit on this host,
// and what a cross-socket read really costs.
//
// CLAUDE.md §IX decides SEQUENTIAL against PARALLEL from three numbers it
// names as roughly 32 KiB, 1 MiB and 32 MiB, and
// concurrent/ParallelismRule.h implements that by reading the host's L1d,
// L2 and L3 sizes out of sysfs and classifying a working set against them.
// Anything below L2 stays sequential; anything inside L3 gets at most four
// workers on one socket; anything past L3 gets a factor derived from how
// many L2-sized pieces the set divides into.
//
// Every one of those is a derivation from cache geometry, and none of them
// is the quantity the rule needs. The rule needs to know where a fork-join
// starts to cost less than the work it saves, and where extra workers stop
// adding bandwidth. Those two are properties of the fork-join cost and the
// memory path, and cache size is only one input to either. On a part whose
// private L2 is a megabyte, a set of a hundred kilobytes already splits
// profitably — because two cores bring two L1 caches, not because the set
// outgrew one L2. And on a part where one core can already saturate its
// cluster's link to DRAM, a set of half a gigabyte gains nothing from a
// second worker on the same cluster however many L2-sized pieces it
// divides into.
//
// So this probe measures the two boundaries directly:
//
//   knee     the smallest working set for which splitting the pass across
//            two cores beats running it inline, by more than the noise
//            floor. Below it, fork-join loses. This is the number
//            ParallelismRule's Sequential/Parallel gate wants.
//
//   ceiling  the largest working set for which that split still wins.
//            Above it, one core has already saturated whatever the
//            bottleneck is, and adding workers on the same cluster adds
//            coherence traffic and nothing else.
//
// Two cores and not more, deliberately. The first decision the rule makes
// is sequential against parallel, which is a two-way question, and a
// bracket measured at two workers is the bracket inside which any split
// can pay. Choosing the FACTOR inside that bracket is a different question
// with a different answer per cluster topology, and pretending one sweep
// answers both would be the same derivation-instead-of-measurement mistake
// this probe exists to correct.
//
// The NUMA half is separate and simpler. The measuring thread never moves:
// it stays on its own core for both halves of the comparison, and only the
// MEMORY moves. A helper thread pinned to a core on the far node touches
// the pages first, so first-touch policy places them there, and then the
// measuring thread reads them from where it always was. Moving the thread
// instead would fold the far node's core, its cache state and its
// scheduler contention into a number that claims to be about distance.
//
// Release behaviour. Every bound in the sweep is a real runtime
// comparison, not an assertion: the sweep indexes a heap region with
// sizes it computed, and a check that compiles out under NDEBUG would be
// no check at all in the build that matters. The contract_assert on the
// helper-thread entry does fire in Release, since Release evaluates
// contracts at `observe` and the handler does not return.

#include <crucible/concurrent/Topology.h>
#include <crucible/ledger/ProbeSupport.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <thread>
#include <vector>

namespace crucible::ledger::probes {

// ── Sweep parameters ──────────────────────────────────────────────────

// The smallest set in the sweep. Below this a pass is a handful of cache
// lines and the measurement is about the loop's prologue.
inline constexpr std::size_t kSweepFloorBytes = 16ull * 1024ull;

// Step. A ratio rather than a doubling, because a doubling can only ever
// locate a boundary to within a factor of two, and the whole point is to
// say where the boundary is rather than which octave it is in.
inline constexpr std::size_t kSweepStepNumerator = 5;
inline constexpr std::size_t kSweepStepDenominator = 4;  // x1.25

inline constexpr std::size_t kSweepCeilingMultipleOfL3 = 8;
inline constexpr std::size_t kSweepMinCeilingBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kSweepMaxCeilingBytes = 256ull * 1024ull * 1024ull;

// A sweep cannot have more points than this. The bound exists so a host
// with an implausible cache report cannot make the probe run for hours.
inline constexpr std::size_t kMaxSweepPoints = 64;

[[nodiscard]] inline std::size_t sweep_ceiling_bytes() noexcept {
    const std::size_t last_level = concurrent::Topology::instance().l3_total_bytes();
    return std::clamp(last_level * kSweepCeilingMultipleOfL3, kSweepMinCeilingBytes, kSweepMaxCeilingBytes);
}

// Samples shrink as the set grows, so every point in the sweep costs
// roughly the same wall time. The floor is the ledger's own minimum,
// because fewer than that is refused at admission and measuring it would
// be time spent producing something unusable.
[[nodiscard]] inline std::size_t samples_for_size(std::size_t bytes) noexcept {
    constexpr std::size_t kWorkBudgetBytes = 64ull * 1024ull * 1024ull;
    const std::size_t wanted = (bytes == 0u) ? 512u : (kWorkBudgetBytes / bytes);
    return std::clamp<std::size_t>(wanted, kMinSampleCount, 512u);
}

// ── The kernel ────────────────────────────────────────────────────────
//
// One 64-bit load per 64-byte line, four independent accumulators. Reading
// one word per line rather than the whole line keeps the loop off the
// vector unit, so the curve is about where the data lives and not about
// how wide the register that received it was. Above the last-level cache
// the hardware fetches the whole line anyway, so the far end of the curve
// is a true bandwidth figure.
//
// noinline because the compiler would otherwise notice that repeated calls
// with the same arguments produce the same sum and compute it once.

[[gnu::noinline]] inline std::uint64_t stream_one_word_per_line(const std::uint64_t* words,
                                                                std::size_t line_count) noexcept {
    constexpr std::size_t kWordsPerLine = 8;
    std::uint64_t lane0 = 0, lane1 = 0, lane2 = 0, lane3 = 0;
    const std::size_t limit = line_count * kWordsPerLine;
    std::size_t index = 0;
    for (; index + 4 * kWordsPerLine <= limit; index += 4 * kWordsPerLine) {
        lane0 += words[index];
        lane1 += words[index + kWordsPerLine];
        lane2 += words[index + 2 * kWordsPerLine];
        lane3 += words[index + 3 * kWordsPerLine];
    }
    for (; index < limit; index += kWordsPerLine) {
        lane0 += words[index];
    }
    return lane0 + lane1 + lane2 + lane3;
}

namespace cache_tier_detail {

// One helper that streams a slice on its own core when told to, and spins
// on an acquire load in between. A spin and not a futex: the wait is the
// thing being measured, and a futex would put a kernel round trip inside
// the fork-join cost this probe exists to find.
class SliceWorker {
public:
    SliceWorker() noexcept = default;
    SliceWorker(SliceWorker const&) = delete("a worker owns its thread; a copy would own the same one twice");
    SliceWorker&
    operator=(SliceWorker const&) = delete("a worker owns its thread; a copy would own the same one twice");
    SliceWorker(SliceWorker&&) = delete("the worker thread captures `this`, so the address must not move");
    SliceWorker& operator=(SliceWorker&&) = delete("the worker thread captures `this`, so the address must not move");

    ~SliceWorker() noexcept { stop(); }

    // Returns false when the CPU is outside the process's allowed set. The
    // caller then measures nothing rather than measuring on a core the
    // scheduler picked and reporting it as if the placement had held.
    [[nodiscard]] bool start(int cpu) noexcept {
        if (thread_.joinable()) {
            return false;
        }
        pinned_ok_.store(false, std::memory_order_relaxed);
        pin_reported_.store(false, std::memory_order_relaxed);
        thread_ = std::thread([this, cpu] { run_(cpu); });
        while (!pin_reported_.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        if (!pinned_ok_.load(std::memory_order_acquire)) {
            stop();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        if (!thread_.joinable()) {
            return;
        }
        quit_.store(true, std::memory_order_relaxed);
        ticket_.fetch_add(1, std::memory_order_release);
        thread_.join();
    }

    void assign(const std::uint64_t* words, std::size_t line_count) noexcept {
        words_ = words;
        line_count_ = line_count;
    }

    // Releases the worker and returns the ticket to wait on. Separated from
    // the wait so the caller can release every worker before doing its own
    // slice, which is what makes this a fork-join and not a relay.
    [[nodiscard]] std::uint32_t release() noexcept { return ticket_.fetch_add(1, std::memory_order_release) + 1u; }

    void wait(std::uint32_t ticket) noexcept {
        while (done_.load(std::memory_order_acquire) != ticket) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }

private:
    void run_(int cpu) noexcept {
        const bool pinned = pin_this_thread_to(cpu);
        pinned_ok_.store(pinned, std::memory_order_release);
        pin_reported_.store(true, std::memory_order_release);
        if (!pinned) {
            return;
        }
        std::uint32_t seen = 0;
        for (;;) {
            while (ticket_.load(std::memory_order_acquire) == seen) {
                if (quit_.load(std::memory_order_relaxed)) {
                    return;
                }
                CRUCIBLE_SPIN_PAUSE;
            }
            seen = ticket_.load(std::memory_order_relaxed);
            if (quit_.load(std::memory_order_relaxed)) {
                return;
            }
            // Fires in Release: contracts build at `observe` there and the
            // violation handler does not return. A worker released with no
            // slice assigned would otherwise read through a null pointer.
            contract_assert(words_ != nullptr);
            sink_ += stream_one_word_per_line(words_, line_count_);
            done_.store(seen, std::memory_order_release);
        }
    }

    // Each cross-thread word on its own line. Sharing one would put a
    // coherence ping-pong inside the fork-join cost, which is the exact
    // quantity being measured.
    alignas(64) std::atomic<std::uint32_t> ticket_{0};
    alignas(64) std::atomic<std::uint32_t> done_{0};
    alignas(64) std::atomic<bool> quit_{false};
    alignas(64) std::atomic<bool> pinned_ok_{false};
    alignas(64) std::atomic<bool> pin_reported_{false};

    const std::uint64_t* words_ = nullptr;
    std::size_t line_count_ = 0;
    // Volatile here and not at the call sites: this one is written by
    // the worker thread and read by nobody, so only a volatile store
    // keeps the kernel's result from being dead code. The measured
    // sites use bench::do_not_optimize instead, which costs nothing.
    volatile std::uint64_t sink_ = 0;
    std::thread thread_{};
};

// Which CPUs a split should use: cores that share a last-level cache with
// the measuring core, so the split's coherence traffic never leaves the
// cluster. Explicit settings win; this is what the daemon gets when nobody
// configured anything.
[[nodiscard]] inline std::vector<int> helper_cores_for(int measuring_cpu) noexcept {
    std::vector<int> chosen;
    const ProbeSettings settings = probe_settings();
    for (const int core : settings.helper_cores) {
        if (core < 0) {
            break;
        }
        chosen.push_back(core);
    }
    if (!chosen.empty()) {
        return chosen;
    }
    for (std::span<const int> group : concurrent::Topology::instance().l3_groups()) {
        const bool holds_measuring_cpu = std::find(group.begin(), group.end(), measuring_cpu) != group.end();
        if (!holds_measuring_cpu) {
            continue;
        }
        for (const int candidate : group) {
            if (candidate != measuring_cpu) {
                chosen.push_back(candidate);
            }
            if (chosen.size() >= kMaxHelperCores) {
                break;
            }
        }
        break;
    }
    return chosen;
}

// A CPU on a different NUMA node from `measuring_cpu`, or -1 when the host
// has one node or the topology could not say.
[[nodiscard]] inline int remote_node_cpu_for(int measuring_cpu) noexcept {
    concurrent::Topology const& topology = concurrent::Topology::instance();
    const int node_count = static_cast<int>(topology.numa_nodes());
    if (node_count < 2) {
        return -1;
    }
    int local_node = -1;
    for (int node = 0; node < node_count; ++node) {
        const std::span<const int> cpus = topology.cores_on_node(node);
        if (std::find(cpus.begin(), cpus.end(), measuring_cpu) != cpus.end()) {
            local_node = node;
            break;
        }
    }
    if (local_node < 0) {
        return -1;
    }
    // The farthest node, not merely a different one. On a host with more
    // than two nodes the hop costs differ, and a penalty measured against
    // the nearest remote node would understate the worst case the
    // placement policy has to survive.
    int best_cpu = -1;
    int best_distance = 0;
    for (int node = 0; node < node_count; ++node) {
        if (node == local_node) {
            continue;
        }
        const std::span<const int> cpus = topology.cores_on_node(node);
        if (cpus.empty()) {
            continue;
        }
        const int distance = topology.numa_distance(local_node, node);
        if (distance > best_distance) {
            best_distance = distance;
            best_cpu = cpus.front();
        }
    }
    return best_cpu;
}

[[nodiscard]] inline int measuring_cpu() noexcept {
    const int configured = probe_settings().pin_core;
    if (configured >= 0) {
        return configured;
    }
    const int current = ::sched_getcpu();
    return current;
}

}  // namespace cache_tier_detail

// ── The measurement ───────────────────────────────────────────────────

struct CacheTierMeasurement {
    LedgerError fault = LedgerError::NotApplicableOnThisHost;

    // Zero means the sweep found no size at which a split paid, which on a
    // host with one usable core is the correct answer and not a failure.
    std::uint64_t parallel_knee_bytes = 0;
    std::uint64_t parallel_ceiling_bytes = 0;
    VerdictEvidence knee_evidence{};
    VerdictEvidence ceiling_evidence{};

    [[nodiscard]] constexpr bool is_usable() const noexcept { return fault == LedgerError::None; }
    [[nodiscard]] constexpr bool found_a_bracket() const noexcept {
        return parallel_knee_bytes != 0u && parallel_ceiling_bytes >= parallel_knee_bytes;
    }
};

struct NumaMeasurement {
    LedgerError fault = LedgerError::NotApplicableOnThisHost;
    // 100 means a remote read costs the same as a local one. 200 means it
    // costs twice as much. Expressed as a cost and not as a gain, because
    // every reader of this number is deciding whether to avoid the hop.
    std::uint32_t remote_cost_percent = 0;
    VerdictEvidence evidence{};

    [[nodiscard]] constexpr bool is_usable() const noexcept { return fault == LedgerError::None; }
};

namespace cache_tier_detail {

inline MeasurementMemo<CacheTierMeasurement> g_sweep_memo{};
inline MeasurementMemo<NumaMeasurement> g_numa_memo{};

// One pass of the sweep. Separated from the measurement because the
// measurement runs it twice: a knee is a byte count, and the only honest
// statement of how reproducible a byte count is, is whether a second
// independent sweep found the same one.
struct SweepPass {
    LedgerError fault = LedgerError::NotApplicableOnThisHost;
    std::uint64_t knee_bytes = 0;
    std::uint64_t ceiling_bytes = 0;
    VerdictEvidence knee_evidence{};
    VerdictEvidence ceiling_evidence{};
};

[[nodiscard]] inline SweepPass run_one_sweep(SliceWorker& worker, ProbeRegion const& region, int self_cpu) noexcept {
    SweepPass pass{};
    auto* words = static_cast<std::uint64_t*>(region.data());
    const std::size_t ceiling = region.size();

    std::uint64_t sink = 0;
    std::size_t points = 0;

    for (std::size_t bytes = kSweepFloorBytes; bytes <= ceiling && points < kMaxSweepPoints;
         bytes = (bytes * kSweepStepNumerator) / kSweepStepDenominator) {
        ++points;
        const std::size_t lines = bytes / 64u;
        if (lines < 8u) {
            continue;
        }
        const std::size_t half = lines / 2u;
        if (half == 0u) {
            continue;
        }
        const std::size_t samples = samples_for_size(bytes);

        auto inline_run = bench::Run{"ledger.cache_tier.inline"};
        (void)inline_run.samples(samples).warmup(8).max_wall_ms(2000);
        if (self_cpu >= 0) {
            (void)inline_run.core(self_cpu);
        }
        const bench::Report inline_report = inline_run.measure([&] {
            sink += stream_one_word_per_line(words, lines);
            bench::do_not_optimize(sink);
        });

        worker.assign(words + (half * 8u), lines - half);
        auto split_run = bench::Run{"ledger.cache_tier.split"};
        (void)split_run.samples(samples).warmup(8).max_wall_ms(2000);
        if (self_cpu >= 0) {
            (void)split_run.core(self_cpu);
        }
        const bench::Report split_report = split_run.measure([&] {
            const std::uint32_t ticket = worker.release();
            sink += stream_one_word_per_line(words, half);
            worker.wait(ticket);
            bench::do_not_optimize(sink);
        });

        const VariantComparison split_pays = compare_variants(inline_report, split_report);
        if (!split_pays.candidate_wins()) {
            continue;
        }
        if (pass.knee_bytes == 0u) {
            pass.knee_bytes = bytes;
            pass.knee_evidence = evidence_from_two_runs(split_report, inline_report);
        }
        pass.ceiling_bytes = bytes;
        pass.ceiling_evidence = evidence_from_two_runs(split_report, inline_report);
    }
    bench::do_not_optimize(sink);
    pass.fault = LedgerError::None;
    return pass;
}

// Folds two sweeps into one answer. The conservative direction differs
// per boundary and the two are folded accordingly.
//
//   knee     the LARGER of the two. A caller that believes the knee is
//            higher than it is stays sequential over a range where it
//            could have split, which costs speed. A caller that believes
//            it is lower forks over a range where forking loses, which
//            costs speed AND breaks the rule's stated promise never to
//            regress. Those are not the same mistake.
//
//   ceiling  the SMALLER of the two, by the same argument run the other
//            way: stopping early leaves speed on the table, and carrying
//            on past the real ceiling adds coherence traffic to a memory
//            path that was already saturated.
//
// The two sweeps' disagreement becomes the run-to-run spread, so a host
// where the knee moves between sweeps gets refused rather than averaged.
[[nodiscard]] inline CacheTierMeasurement fold_sweeps(SweepPass const& first, SweepPass const& second) noexcept {
    CacheTierMeasurement folded{};
    if (first.fault != LedgerError::None) {
        folded.fault = first.fault;
        return folded;
    }
    if (second.fault != LedgerError::None) {
        folded.fault = second.fault;
        return folded;
    }
    folded.fault = LedgerError::None;

    if (first.knee_bytes == 0u || second.knee_bytes == 0u) {
        // One sweep found no profitable split. Reporting the other sweep's
        // bracket would be reporting the run that happened to be luckier.
        return folded;
    }

    folded.parallel_knee_bytes = std::max(first.knee_bytes, second.knee_bytes);
    folded.parallel_ceiling_bytes = std::min(first.ceiling_bytes, second.ceiling_bytes);

    folded.knee_evidence = (first.knee_bytes >= second.knee_bytes) ? first.knee_evidence : second.knee_evidence;
    folded.knee_evidence.within_run_cv_ppm =
        std::max(first.knee_evidence.within_run_cv_ppm, second.knee_evidence.within_run_cv_ppm);
    folded.knee_evidence.run_to_run_spread_ppm =
        spread_ppm(static_cast<double>(first.knee_bytes), static_cast<double>(second.knee_bytes));
    folded.knee_evidence.sample_count = std::min(first.knee_evidence.sample_count, second.knee_evidence.sample_count);

    folded.ceiling_evidence =
        (first.ceiling_bytes <= second.ceiling_bytes) ? first.ceiling_evidence : second.ceiling_evidence;
    folded.ceiling_evidence.within_run_cv_ppm =
        std::max(first.ceiling_evidence.within_run_cv_ppm, second.ceiling_evidence.within_run_cv_ppm);
    folded.ceiling_evidence.run_to_run_spread_ppm =
        spread_ppm(static_cast<double>(first.ceiling_bytes), static_cast<double>(second.ceiling_bytes));
    folded.ceiling_evidence.sample_count =
        std::min(first.ceiling_evidence.sample_count, second.ceiling_evidence.sample_count);
    return folded;
}

[[nodiscard]] inline CacheTierMeasurement measure_sweep() noexcept {
    CacheTierMeasurement result{};

    // An instrumented build measures its own instrumentation. Refer to
    // kBuildIsInstrumented in ProbeSupport.h for what it costs.
    if constexpr (kBuildIsInstrumented) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    const int self_cpu = measuring_cpu();
    const std::vector<int> helpers = helper_cores_for(self_cpu);
    if (helpers.empty()) {
        // One usable core. "Never split" is the right answer and the
        // ledger should say so rather than pretending it measured.
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    SliceWorker worker{};
    if (!worker.start(helpers.front())) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    auto region = ProbeRegion::create(sweep_ceiling_bytes(), PagePolicy::BasePages);
    if (!region.has_value()) {
        worker.stop();
        result.fault = region.error();
        return result;
    }
    (void)region->fault_in(1u);

    const SweepPass first = run_one_sweep(worker, *region, self_cpu);
    const SweepPass second = run_one_sweep(worker, *region, self_cpu);
    worker.stop();
    return fold_sweeps(first, second);
}

[[nodiscard]] inline NumaMeasurement measure_numa() noexcept {
    NumaMeasurement result{};

    // An instrumented build measures its own instrumentation. Refer to
    // kBuildIsInstrumented in ProbeSupport.h.
    if constexpr (kBuildIsInstrumented) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    const int self_cpu = measuring_cpu();
    const int remote_cpu = remote_node_cpu_for(self_cpu);
    if (remote_cpu < 0) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    // Past the last-level cache in both cases, or the comparison is
    // between two cache hits and says nothing about the interconnect.
    const std::size_t bytes = sweep_ceiling_bytes();

    auto local_region = ProbeRegion::create(bytes, PagePolicy::BasePages);
    if (!local_region.has_value()) {
        result.fault = local_region.error();
        return result;
    }
    auto remote_region = ProbeRegion::create(bytes, PagePolicy::BasePages);
    if (!remote_region.has_value()) {
        result.fault = remote_region.error();
        return result;
    }

    // The measuring thread touches the local region, so first-touch places
    // it on the measuring thread's node.
    (void)local_region->fault_in(1u);

    // A thread on the far node touches the other one, so first-touch
    // places it there. It exists only to fault the pages; the measurement
    // that follows runs on the original core for both regions.
    bool remote_placed = false;
    {
        ProbeRegion* target = &*remote_region;
        std::thread toucher{[target, remote_cpu, &remote_placed] {
            if (!pin_this_thread_to(remote_cpu)) {
                return;
            }
            (void)target->fault_in(1u);
            remote_placed = true;
        }};
        toucher.join();
    }
    if (!remote_placed) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    const std::size_t lines = bytes / 64u;
    // Not volatile. bench::do_not_optimize is the barrier; a volatile
    // accumulator would put a store inside the timed body that the
    // code being measured does not have.
    std::uint64_t sink = 0;

    auto measure_region = [&](const char* name, ProbeRegion const& region) {
        auto run = bench::Run{name};
        (void)run.samples(std::max<std::size_t>(kMinSampleCount, 48u)).warmup(2).max_wall_ms(10000);
        if (self_cpu >= 0) {
            (void)run.core(self_cpu);
        }
        auto* region_words = static_cast<const std::uint64_t*>(region.data());
        return run.measure([&] {
            sink += stream_one_word_per_line(region_words, lines);
            bench::do_not_optimize(sink);
        });
    };

    const bench::Report local_first = measure_region("ledger.numa.local.run1", *local_region);
    const bench::Report remote_first = measure_region("ledger.numa.remote.run1", *remote_region);
    const bench::Report local_second = measure_region("ledger.numa.local.run2", *local_region);
    const bench::Report remote_second = measure_region("ledger.numa.remote.run2", *remote_region);
    bench::do_not_optimize(sink);

    // Cost, not gain: what the remote read costs as a percentage of the
    // local one, so a host where the hop is free answers 100 and one
    // where it doubles the cost answers 200. Every caller of this number
    // is deciding whether to avoid the hop, and a ratio that went the
    // other way would have them reading 50 as a penalty.
    //
    // The better of each pair goes into the ratio. Taking the best local
    // against the best remote is the conservative pairing for a penalty:
    // it compares each side at its most favourable, so the penalty
    // reported is the smallest the two runs support rather than the
    // largest.
    const double best_local = std::min(local_first.pct.p50, local_second.pct.p50);
    const double best_remote = std::min(remote_first.pct.p50, remote_second.pct.p50);
    result.remote_cost_percent = gain_percent(best_remote, best_local);
    // The verdict is a ratio, so its evidence has to say whether the ratio
    // reproduced and not merely whether the remote runs were steady. The
    // baseline is the remote side here, because the reported number is a
    // cost and gain_percent was handed the pair in that order.
    result.evidence = evidence_for_ratio(remote_first, local_first, remote_second, local_second);
    result.fault = LedgerError::None;
    return result;
}

[[nodiscard]] inline CacheTierMeasurement const& shared_sweep() noexcept {
    return g_sweep_memo.get_or_measure(&measure_sweep);
}

[[nodiscard]] inline NumaMeasurement const& shared_numa() noexcept { return g_numa_memo.get_or_measure(&measure_numa); }

}  // namespace cache_tier_detail

// ── The three ProbeFunctions ──────────────────────────────────────────

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_parallel_knee_bytes(CompetenceReport const&) noexcept {
    CacheTierMeasurement const& measured = cache_tier_detail::shared_sweep();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (!measured.found_a_bracket()) {
        // No size in the sweep split profitably. That is an answer, but it
        // is not a byte count, and storing a zero would read as "split
        // everything" to a caller that did not check.
        return std::unexpected(LedgerError::NotApplicableOnThisHost);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.parallel_knee_bytes}, .evidence = measured.knee_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_parallel_ceiling_bytes(CompetenceReport const&) noexcept {
    CacheTierMeasurement const& measured = cache_tier_detail::shared_sweep();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (!measured.found_a_bracket()) {
        return std::unexpected(LedgerError::NotApplicableOnThisHost);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.parallel_ceiling_bytes},
                              .evidence = measured.ceiling_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_numa_remote_cost(CompetenceReport const&) noexcept {
    NumaMeasurement const& measured = cache_tier_detail::shared_numa();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (measured.remote_cost_percent == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.remote_cost_percent}, .evidence = measured.evidence};
}

namespace cache_tier_detail::self_test {

// A worker owns a thread that captured `this`, so neither a copy nor a
// move may exist.
static_assert(!std::is_copy_constructible_v<SliceWorker>);
static_assert(!std::is_move_constructible_v<SliceWorker>);

// A default measurement is not usable and does not claim a bracket, so a
// caller that forgets to check is_usable() still cannot read a byte count
// out of a probe that never ran.
static_assert(!CacheTierMeasurement{}.is_usable());
static_assert(!CacheTierMeasurement{}.found_a_bracket());
static_assert(!NumaMeasurement{}.is_usable());

// A ceiling below its knee is not a bracket. The sweep assigns the
// ceiling on every winning point after the knee, so this can only come
// from a corrupted structure, and the predicate catches it rather than
// handing a caller an inverted range.
static_assert(!CacheTierMeasurement{
    .fault = LedgerError::None, .parallel_knee_bytes = 1024, .parallel_ceiling_bytes = 512}
                   .found_a_bracket());
static_assert(CacheTierMeasurement{
    .fault = LedgerError::None, .parallel_knee_bytes = 1024, .parallel_ceiling_bytes = 1024}
                  .found_a_bracket());

// The sweep must actually step. A ratio that rounds back to itself would
// loop forever on the smallest size.
static_assert(kSweepStepNumerator > kSweepStepDenominator);
static_assert((kSweepFloorBytes * kSweepStepNumerator) / kSweepStepDenominator > kSweepFloorBytes);

}  // namespace cache_tier_detail::self_test

}  // namespace crucible::ledger::probes
