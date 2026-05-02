// ═══════════════════════════════════════════════════════════════════
// bench_scheduler_policies — three-axis comparison of the seven
// concurrent::scheduler policies.
//
// For each of {Fifo, Lifo, RoundRobin, LocalityAware,
//              Deadline<K>, Cfs<K>, Eevdf<K>}, three orthogonal
// measurements:
//
//   FLOOR  — single-thread per-op latency (no contention)
//            Reports p50/p99/p99.9 from bench::run's per-call
//            timing.  This is the steady-state cost of a single
//            try_push, with the handle long-lived (no pool churn) —
//            the minimum the policy can do per submit.
//
//   TAIL   — per-submit latency distribution UNDER contention
//            4 producers (or 1 owner + 4 thieves for Lifo) racing,
//            each producer rdtsc-stamps every try_push and we
//            aggregate samples post-join.  The p99/p99.9 tail under
//            load reveals contention shape — Fifo's single head
//            cliffs, LocalityAware's grid stays flat.
//
//   THRU   — wall-clock per spawn-drain-join cycle (throughput)
//            bench::Run.measure() times the entire cycle: spawn N
//            threads, drain to total, join.  Items per cycle is
//            constant per policy so wall-clock is comparable as
//            "ms to drain the same backlog".
//
// Honest caveats:
//   * Each scheduler::Policy::queue_template<Job> IS one of the
//     Permissioned wrappers with a distinct UserTag; runtime cost is
//     identical to the bare wrapper.  This bench is the
//     scheduler-policy view of the same underlying primitives — not
//     new performance numbers, but a uniform comparison surface.
//   * Cfs and Eevdf use the same calendar grid as Deadline; their
//     numbers are statistically equivalent (different intent, same
//     queue topology).  Reporting all three is honest accounting,
//     not redundant measurement.
//   * Lifo's "producer" is the owner thread; the 4 "consumers" are
//     thieves stealing from the top.  This matches the policy's
//     intended use shape (recursive fork-join with cache-hot owner).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/scheduler/Policies.h>
#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include "bench_harness.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <tuple>
#include <vector>

namespace cs = crucible::concurrent::scheduler;
using namespace crucible::concurrent;
using namespace crucible::safety;

namespace {

// ── must_lend_ helper ─────────────────────────────────────────────
//
// Pool-based wrappers (Mpmc/Mpsc/ChaseLev) return std::optional<Handle>
// from their lend factories.  The handle is move-only with NO move-
// assign (Pinned-like) — std::optional<T>::operator= requires T's
// move-assign, so the natural retry pattern
//    auto h = ch.producer(); while (!h) h = ch.producer();
// fails to compile.  In bench context the lend always succeeds
// (no with_drained_access in flight), so abort on failure rather
// than retry.

template <typename T>
[[nodiscard, gnu::noinline]] T must_lend_(std::optional<T>&& opt,
                                          const char* what) noexcept {
    if (!opt) [[unlikely]] {
        std::fprintf(stderr, "must_lend_(%s): pool unexpectedly empty\n", what);
        std::abort();
    }
    return std::move(*opt);
}

// ── Workload sizes ────────────────────────────────────────────────
//
// Tuned so each cycle takes ~1-5 ms (60+ samples for stable p99 in a
// few seconds), and total bench runs in <60s.  Sizes per shape:
//
//   FIFO/MPMC/RoundRobin: 4 producers × 2K = 8K items
//   Lifo (ChaseLev):      1 owner pushing 2K, 4 thieves
//   LocalityAware:        4 producers × 2K = 8K through 4×4 grid
//   Priority (Cal grid):  4 producers × 600 = 2400 (fits 1024-bucket window)
//
// All deliberately sized to fit each policy's queue_template default
// capacity.

// Sample sizes are chosen so spawn overhead (~150 µs/jthread × N)
// drops below 2% of total wall time — at that point, "items / wall"
// is a true steady-state throughput rate (workers are warm, the
// ramp-up cost amortizes to zero).  Smaller workloads conflate
// "queue throughput" with "pthread_create cost".
//
// Estimated wall per cycle at expected per-policy steady-state rate:
//   FIFO/RR/LA at ~15-35 M items/s × 200K items = 6-13 ms
//   Lifo at ~25 M × 50K = 2 ms
//   Priority single-grid at ~18 M × 80K = 4 ms
//   Per-shard at ~25 M × 80K = 3 ms
// Spawn ~1.2 ms per cycle is now <30% even for the smallest case.
constexpr std::size_t   ITEMS_FIFO_PER_PROD     = 50'000;
constexpr std::size_t   ITEMS_LIFO              = 50'000;
constexpr std::size_t   ITEMS_PRIORITY_PER_PROD = 20'000;
constexpr std::size_t   NUM_PRODUCERS           = 4;
constexpr std::size_t   NUM_CONSUMERS           = 4;
constexpr std::size_t   NUM_THIEVES             = 4;

// One long-lived contended run per policy.  Workers spawn once,
// drain ~all items, join.  No per-iter pthread_create cost on the
// measurement.  Tail percentiles get ~6250 samples per producer
// (200K / 32) which is enough for stable p99.9 — outliers can't
// dominate.
constexpr std::size_t   CONTENDED_ITERATIONS    = 1;

// Legacy spawn-bound throughput constants — referenced by the
// dead bench_throughput_*_ templates left in place for reference.
// These templates are no longer called from main(); each cycle
// includes pthread_create/join cost and was misleading.  Steady-
// state throughput now comes from contended (long-lived workers).
constexpr std::size_t   THRU_SAMPLES            = 30;
constexpr std::size_t   THRU_WARMUP             = 2;
constexpr std::size_t   THRU_MAX_WALL_MS        = 8000;

// rdtsc has ~30-cycle (~10ns) resolution on Zen — bracketing single
// pushes quantizes per-op cost to multiples of the resolution.
// Batching K pushes per rdtsc pair amortizes the per-op cost above
// the noise floor.  K=32 is the sweet spot: large enough that
// 32 * (5ns push) = 160ns >> 10ns rdtsc floor, small enough that
// per-batch wall time stays in the L1d-resident regime so cache-state
// transitions don't widen the variance.
constexpr std::uint32_t BATCH_PER_PROD          = 32;

// ── Job types ─────────────────────────────────────────────────────

struct SimpleJob {
    std::uint64_t v = 0;
};
static_assert(std::is_trivially_copyable_v<SimpleJob>);

struct PriorityJob {
    std::uint32_t pid = 0;
    std::uint32_t seq = 0;
    std::uint64_t key = 0;
};
static_assert(std::is_trivially_copyable_v<PriorityJob>);

struct PriorityKey {
    static std::uint64_t key(const PriorityJob& j) noexcept { return j.key; }
};

// Concrete priority-keyed scheduler instantiations.  Capacity tuned
// to fit ITEMS_PRIORITY_PER_PROD × NUM_PRODUCERS = 2400 in the bucket
// window without wraparound.
using SchedDeadline = cs::Deadline<PriorityKey, 4, 1024, 64, 1000ULL>;
using SchedCfs      = cs::Cfs<PriorityKey,      4, 1024, 64, 1000ULL>;
using SchedEevdf    = cs::Eevdf<PriorityKey,    4, 1024, 64, 1000ULL>;

// Per-shard variants — N independent calendars, each with its own
// current_bucket.  Smaller per-shard buckets/cap (4×64×16 = 4096
// per shard, ~96KB) — comparable total memory but no cross-thread
// reads on push path.
using SchedDeadlinePerShard = cs::DeadlinePerShard<PriorityKey, 4, 64, 16, 1000ULL>;
using SchedCfsPerShard      = cs::CfsPerShard<PriorityKey,      4, 64, 16, 1000ULL>;
using SchedEevdfPerShard    = cs::EevdfPerShard<PriorityKey,    4, 64, 16, 1000ULL>;

// ── Per-submit latency aggregation ────────────────────────────────

struct ContendedResult {
    bench::Percentiles per_op;
    double             total_wall_ms = 0.0;
    std::size_t        total_items   = 0;
    double             items_per_sec = 0.0;
};

[[nodiscard]] ContendedResult aggregate_samples_(
    std::vector<std::vector<std::uint64_t>>&& per_thread_cycles,
    double wall_ms,
    std::size_t total_items)
{
    ContendedResult r;
    r.total_wall_ms = wall_ms;
    r.total_items   = total_items;
    r.items_per_sec = (wall_ms > 0)
        ? static_cast<double>(total_items) * 1000.0 / wall_ms
        : 0.0;

    std::vector<double> ns;
    std::size_t total_samples = 0;
    for (auto const& v : per_thread_cycles) total_samples += v.size();
    ns.reserve(total_samples);

    // Subtract the rdtsc back-to-back overhead (~10ns on Zen 3/4) from
    // every per-op sample.  Without this, tail p50 sits at exactly
    // ovh_ns regardless of actual push cost — measuring the timestamp
    // primitive, not the queue.  Identical correction to what
    // bench::Run::measure does internally for its own samples.
    const double      nspc = bench::Timer::ns_per_cycle();
    const std::uint64_t ovh = bench::Timer::overhead_cycles();
    for (auto const& vec : per_thread_cycles) {
        for (auto c : vec) {
            const std::uint64_t adj = (c > ovh) ? (c - ovh) : 0;
            ns.push_back(static_cast<double>(adj) * nspc);
        }
    }
    r.per_op = bench::Percentiles::compute(ns);
    return r;
}

// ── Batched aggregator ────────────────────────────────────────────
//
// Each per-thread vector contains *batch-amortized* per-op cycles —
// one sample per BATCH pushes, divided.  Resolves the rdtsc resolution
// problem: a single rdtsc-bracketed push has ~30-cycle (~10ns)
// quantization noise; the smallest non-zero observable per-op cost is
// the rdtsc resolution itself.  Batching K=32 pushes per timer pair
// gives true per-op cost above the noise floor (K * cycles per batch
// is ~5-100x the resolution).

[[nodiscard]] ContendedResult aggregate_batched_(
    std::vector<std::vector<double>>&& per_thread_per_op_ns,
    double wall_ms,
    std::size_t total_items)
{
    ContendedResult r;
    r.total_wall_ms = wall_ms;
    r.total_items   = total_items;
    r.items_per_sec = (wall_ms > 0)
        ? static_cast<double>(total_items) * 1000.0 / wall_ms
        : 0.0;

    std::vector<double> ns;
    std::size_t total_samples = 0;
    for (auto const& v : per_thread_per_op_ns) total_samples += v.size();
    ns.reserve(total_samples);
    for (auto const& vec : per_thread_per_op_ns) {
        for (double v : vec) ns.push_back(v);
    }
    r.per_op = bench::Percentiles::compute(ns);
    return r;
}

// ── Topology pinning ─────────────────────────────────────────────
//
// Ryzen 9 5950X = 16 physical cores split into 2 CCDs (L3 groups),
// each CCD has 8 cores + SMT siblings.  Cross-CCD atomics travel
// over Infinity Fabric (~80-100ns).  Same-CCD different-core stays
// in the shared L3 (~10-15ns).  SMT siblings share L1d but contend
// for LSU/LSD throughput.
//
// Layout strategy for 4 producer + 4 consumer pairs:
//   * Pick the largest L3 group (one CCD) — keeps all 8 threads in
//     the same L3 cache so cross-thread current_bucket reads are
//     L3-resident, not cross-CCD DRAM.
//   * Pair (producer<S>, consumer<S>) onto two physical cores in
//     that CCD.  NOT SMT siblings (avoids LSU contention).  With 8
//     physical cores per CCD, producer<S> on cpus[S], consumer<S>
//     on cpus[S+4] — same CCD, separate L1/L2.

struct PinningLayout {
    bool                              valid = false;
    std::size_t                       l3_group_id = 0;
    std::size_t                       l3_size = 0;
    std::array<int, NUM_PRODUCERS>    producer_cpu{-1, -1, -1, -1};
    std::array<int, NUM_PRODUCERS>    consumer_cpu{-1, -1, -1, -1};
};

[[nodiscard]] PinningLayout choose_layout_() noexcept {
    PinningLayout layout{};
    auto const& topo = Topology::instance();
    auto groups = topo.l3_groups();
    if (groups.empty()) return layout;

    // Pick the largest L3 group.
    std::size_t best_idx = 0;
    std::size_t best_sz  = groups[0].size();
    for (std::size_t i = 1; i < groups.size(); ++i) {
        if (groups[i].size() > best_sz) {
            best_idx = i;
            best_sz  = groups[i].size();
        }
    }
    auto const& chosen = groups[best_idx];
    layout.l3_group_id = best_idx;
    layout.l3_size     = chosen.size();

    // Need 2 * NUM_PRODUCERS distinct cpus.  Strategy: take the
    // first NUM_PRODUCERS as producers, next NUM_PRODUCERS as
    // consumers (skip SMT siblings — sysfs lists physical cores
    // first, then siblings, but l3_groups concatenates both, so
    // taking cpus[0..7] from a CCD on Zen gives us 8 physical cores
    // (sibling cpus 16-23 follow); we want non-sibling pairs).
    constexpr std::size_t needed = 2 * NUM_PRODUCERS;
    if (chosen.size() < needed) return layout;
    for (std::size_t s = 0; s < NUM_PRODUCERS; ++s) {
        layout.producer_cpu[s] = chosen[s];
        layout.consumer_cpu[s] = chosen[s + NUM_PRODUCERS];
    }
    layout.valid = true;
    return layout;
}

[[nodiscard]] bool pin_thread_to_cpu_(std::thread::id /*tid_unused*/, int cpu) noexcept {
    if (cpu < 0) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    return ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set) == 0;
}

// SCHED_FIFO was tried as a way to remove OS preemption noise.  It
// works for non-contending workloads (Lifo, per-shard) but DEADLOCKS
// any policy whose producers contend on a shared head (Fifo's single
// MPMC ring): 4 producers + 4 consumers all at RT priority 50 spin
// indefinitely against each other in the SCQ inner-CAS retry loop
// because std::this_thread::yield() between RT threads of the same
// priority on different cores is a no-op — observed Fifo p99.9 going
// 768 ns → 22 ms with RT enabled.  Decision: pinning alone, no RT.
// The remaining tail outliers (RoundRobin's ~140 µs at p99.9) ARE
// real OS preemption; production code that needs predictable tail
// MUST add CPU isolation (isolcpus= boot param) — RT priority is not
// a substitute for that.

void print_pinning_layout_(const PinningLayout& L) noexcept {
    if (!L.valid) {
        std::printf("  pinning: DISABLED (topology probe found no usable L3 group)\n");
        return;
    }
    std::printf("  pinning: L3-group[%zu] (%zu cpus)\n",
                L.l3_group_id, L.l3_size);
    for (std::size_t s = 0; s < NUM_PRODUCERS; ++s) {
        std::printf("    shard[%zu]: producer cpu=%d  consumer cpu=%d\n",
                    s, L.producer_cpu[s], L.consumer_cpu[s]);
    }
}

// ── Result row aggregation per policy ─────────────────────────────

struct PolicyResults {
    const char*        policy_name;
    bench::Report      floor;          // single-thread per-op (push+pop)
    ContendedResult    tail;           // contended per-submit, K iterations
                                       // (steady-state items_per_sec inside)
};

void print_policy_table_header_() {
    std::printf("\n%-18s | %10s %10s %10s | %10s %10s %10s | %14s\n",
                "policy",
                "floor p50", "floor p99", "floor p99.9",
                "tail p50",  "tail p99",  "tail p99.9",
                "steady-state");
    std::printf("%-18s | %10s %10s %10s | %10s %10s %10s | %14s\n",
                "──────────────────",
                "(ns)", "(ns)", "(ns)",
                "(ns)", "(ns)", "(ns)",
                "(M items/s)");
}

void print_policy_row_(const PolicyResults& r) {
    // Steady-state throughput from contended: total items processed
    // over the SUM of contended iteration walls (NOT spawn-bound).
    const double items_per_sec_M = r.tail.items_per_sec / 1e6;
    std::printf("%-18s | %10.1f %10.1f %10.1f | %10.1f %10.1f %10.1f | %14.2f\n",
                r.policy_name,
                r.floor.pct.p50,
                r.floor.pct.p99,
                r.floor.pct.p99_9,
                r.tail.per_op.p50,
                r.tail.per_op.p99,
                r.tail.per_op.p99_9,
                items_per_sec_M);
}

// ═════════════════════════════════════════════════════════════════
// Runner: Permissioned MPMC (Fifo, Cfs/Eevdf scaffold-fallback).
// Long-lived producer/consumer handles for floor; multiple per-thread
// for contended.
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_pmpmc_(const char* name, Channel& ch) {
    auto producer = must_lend_(ch.producer(), "fifo producer");
    auto consumer = must_lend_(ch.consumer(), "fifo consumer");
    SimpleJob j{0};
    return bench::run(name, [&]{
        const bool ok = producer.try_push(j);
        bench::do_not_optimize(ok);
        auto popped = consumer.try_pop();
        bench::do_not_optimize(popped);
        ++j.v;
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_pmpmc_(
    Channel& ch, const PinningLayout& layout)
{
    constexpr std::size_t TOTAL_PER_ITER = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;
    constexpr std::size_t batches_per_prod =
        (ITEMS_FIFO_PER_PROD + BATCH_PER_PROD - 1) / BATCH_PER_PROD;

    std::vector<std::vector<double>> per_prod_ns(NUM_PRODUCERS);
    for (auto& v : per_prod_ns) v.reserve(batches_per_prod * CONTENDED_ITERATIONS);

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>        start{false};
        std::atomic<std::size_t> consumed{0};

        std::vector<std::jthread> producers;
        for (std::size_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
            const int cpu = layout.producer_cpu[pid];
            producers.emplace_back([&, pid, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                auto handle = must_lend_(ch.producer(), "fifo producer (contended)");
                const double      nspc = bench::Timer::ns_per_cycle();
                const std::uint64_t ovh = bench::Timer::overhead_cycles();
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; s += BATCH_PER_PROD) {
                    const std::size_t end = std::min<std::size_t>(
                        s + BATCH_PER_PROD, ITEMS_FIFO_PER_PROD);
                    const std::size_t cnt = end - s;
                    const auto t0 = bench::rdtsc_start();
                    for (std::size_t k = 0; k < cnt; ++k) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | (s + k))};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                    const auto t1 = bench::rdtsc_end();
                    const std::uint64_t raw = t1 - t0;
                    const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
                    per_prod_ns[pid].push_back(
                        (static_cast<double>(adj) * nspc) / static_cast<double>(cnt));
                }
            });
        }

        std::vector<std::jthread> consumers;
        for (std::size_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            const int cpu = layout.consumer_cpu[cid];
            consumers.emplace_back([&, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                auto handle = must_lend_(ch.consumer(), "fifo consumer (contended)");
                while (consumed.load(std::memory_order_acquire) < TOTAL_PER_ITER) {
                    if (auto opt = handle.try_pop()) {
                        bench::do_not_optimize(opt->v);
                        consumed.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }

        const auto t_start = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        for (auto& p : producers) p.join();
        for (auto& c : consumers) c.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += TOTAL_PER_ITER;
    }

    return aggregate_batched_(std::move(per_prod_ns), total_wall_ms, total_items);
}

// Channel constructed once by the caller and reused across every
// bench iteration — the queue is empty between iterations because
// each iteration drains exactly TOTAL items.  Hoisting matters for
// calendar grids where allocation alone (~13 MB malloc + zero) is
// 3-10× the actual workload cost.
template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_pmpmc_(
    const char* name, Channel& ch)
{
    return bench::Run{name}
        .samples(THRU_SAMPLES)
        .warmup(THRU_WARMUP)
        .batch(1)
        .no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{
            constexpr std::size_t TOTAL = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;

            std::atomic<bool>           start{false};
            std::atomic<std::size_t>    consumed{0};

            std::vector<std::jthread> producers;
            for (std::size_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
                producers.emplace_back([&, pid](std::stop_token) noexcept {
                    auto handle = must_lend_(ch.producer(), "fifo producer (thru)");
                    while (!start.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; ++s) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | s)};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                });
            }
            std::vector<std::jthread> consumers;
            for (std::size_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
                consumers.emplace_back([&](std::stop_token) noexcept {
                    auto handle = must_lend_(ch.consumer(), "fifo consumer (thru)");
                    while (consumed.load(std::memory_order_acquire) < TOTAL) {
                        if (auto opt = handle.try_pop()) {
                            bench::do_not_optimize(opt->v);
                            consumed.fetch_add(1, std::memory_order_acq_rel);
                        } else {
                            std::this_thread::yield();
                        }
                    }
                });
            }
            start.store(true, std::memory_order_release);
            for (auto& p : producers) p.join();
            for (auto& c : consumers) c.join();
        });
}

// ═════════════════════════════════════════════════════════════════
// Runner: Permissioned MPSC (RoundRobin).
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_pmpsc_(const char* name, Channel& ch) {
    auto producer = must_lend_(ch.producer(), "rr producer");
    auto cons_perm = mint_permission_root<typename Channel::consumer_tag>();
    auto consumer  = ch.consumer(std::move(cons_perm));
    SimpleJob j{0};
    return bench::run(name, [&]{
        const bool ok = producer.try_push(j);
        bench::do_not_optimize(ok);
        auto popped = consumer.try_pop();
        bench::do_not_optimize(popped);
        ++j.v;
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_pmpsc_(
    Channel& ch, const PinningLayout& layout)
{
    constexpr std::size_t TOTAL_PER_ITER = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;
    constexpr std::size_t batches_per_prod =
        (ITEMS_FIFO_PER_PROD + BATCH_PER_PROD - 1) / BATCH_PER_PROD;

    auto cons_perm = mint_permission_root<typename Channel::consumer_tag>();
    auto consumer  = ch.consumer(std::move(cons_perm));

    std::vector<std::vector<double>> per_prod_ns(NUM_PRODUCERS);
    for (auto& v : per_prod_ns) v.reserve(batches_per_prod * CONTENDED_ITERATIONS);

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>        start{false};
        std::atomic<std::size_t> consumed{0};

        std::vector<std::jthread> producers;
        for (std::size_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
            const int cpu = layout.producer_cpu[pid];
            producers.emplace_back([&, pid, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                auto handle = must_lend_(ch.producer(), "rr producer (contended)");
                const double      nspc = bench::Timer::ns_per_cycle();
                const std::uint64_t ovh = bench::Timer::overhead_cycles();
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; s += BATCH_PER_PROD) {
                    const std::size_t end = std::min<std::size_t>(
                        s + BATCH_PER_PROD, ITEMS_FIFO_PER_PROD);
                    const std::size_t cnt = end - s;
                    const auto t0 = bench::rdtsc_start();
                    for (std::size_t k = 0; k < cnt; ++k) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | (s + k))};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                    const auto t1 = bench::rdtsc_end();
                    const std::uint64_t raw = t1 - t0;
                    const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
                    per_prod_ns[pid].push_back(
                        (static_cast<double>(adj) * nspc) / static_cast<double>(cnt));
                }
            });
        }

        std::jthread cons_t([&, cpu = layout.consumer_cpu[0]](std::stop_token) noexcept {
            (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
            while (consumed.load(std::memory_order_acquire) < TOTAL_PER_ITER) {
                if (auto opt = consumer.try_pop()) {
                    bench::do_not_optimize(opt->v);
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                } else {
                    std::this_thread::yield();
                }
            }
        });

        const auto t_start = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        for (auto& p : producers) p.join();
        cons_t.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += TOTAL_PER_ITER;
    }

    return aggregate_batched_(std::move(per_prod_ns), total_wall_ms, total_items);
}

template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_pmpsc_(
    const char* name, Channel& ch)
{
    using Ch = Channel;
    constexpr std::size_t TOTAL = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;

    // Consumer permission minted ONCE outside the bench loop —
    // re-mint per iter would be a fresh "root" permission which the
    // wrapper rejects (only one linear consumer per channel).  Pattern
    // matches: the consumer thread inside the body lends from this
    // permanent permission.
    auto cons_perm = mint_permission_root<typename Ch::consumer_tag>();
    auto consumer  = ch.consumer(std::move(cons_perm));

    return bench::Run{name}
        .samples(THRU_SAMPLES).warmup(THRU_WARMUP).batch(1).no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{

            std::atomic<bool>           start{false};
            std::atomic<std::size_t>    consumed{0};
            std::vector<std::jthread>   producers;
            for (std::size_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
                producers.emplace_back([&, pid](std::stop_token) noexcept {
                    auto handle = must_lend_(ch.producer(), "rr producer (thru)");
                    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                    for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; ++s) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | s)};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                });
            }
            std::jthread cons_t([&](std::stop_token) noexcept {
                while (consumed.load(std::memory_order_acquire) < TOTAL) {
                    if (auto opt = consumer.try_pop()) {
                        bench::do_not_optimize(opt->v);
                        consumed.fetch_add(1, std::memory_order_acq_rel);
                    } else { std::this_thread::yield(); }
                }
            });
            start.store(true, std::memory_order_release);
            for (auto& p : producers) p.join();
            cons_t.join();
        });
}

// ═════════════════════════════════════════════════════════════════
// Runner: Permissioned ChaseLevDeque (Lifo).
// Owner is the "producer".  Floor measures owner's try_push (no
// thieves).  Contended measures owner's try_push WHILE thieves are
// stealing from top.
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_lifo_(const char* name, Channel& deq) {
    auto owner_perm = mint_permission_root<typename Channel::owner_tag>();
    auto owner = deq.owner(std::move(owner_perm));
    std::uint64_t v = 0;
    return bench::run(name, [&]{
        if (!owner.try_push(v)) {
            (void)owner.try_pop();
            (void)owner.try_push(v);
        }
        auto popped = owner.try_pop();
        bench::do_not_optimize(popped);
        ++v;
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_lifo_(
    Channel& deq, const PinningLayout& layout)
{
    constexpr std::size_t batches_per_owner =
        (ITEMS_LIFO + BATCH_PER_PROD - 1) / BATCH_PER_PROD;

    auto owner_perm = mint_permission_root<typename Channel::owner_tag>();
    auto owner = deq.owner(std::move(owner_perm));

    // Owner runs in the bench main thread — pin once, lasts across iters.
    (void)pin_thread_to_cpu_(std::this_thread::get_id(),
                             layout.producer_cpu[0]);

    std::vector<std::vector<double>> per_thread_ns(1);
    per_thread_ns[0].reserve(batches_per_owner * CONTENDED_ITERATIONS);

    const double      nspc = bench::Timer::ns_per_cycle();
    const std::uint64_t ovh = bench::Timer::overhead_cycles();

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>          start_thieves{false};
        std::atomic<bool>          stop_thieves{false};
        std::atomic<std::uint64_t> steal_count{0};

        std::vector<std::jthread> thieves;
        for (std::size_t t = 0; t < NUM_THIEVES; ++t) {
            const int cpu = layout.consumer_cpu[t];
            thieves.emplace_back([&, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                auto handle = must_lend_(deq.thief(), "lifo thief (contended)");
                while (!start_thieves.load(std::memory_order_acquire))
                    std::this_thread::yield();
                while (!stop_thieves.load(std::memory_order_acquire)) {
                    if (auto v = handle.try_steal()) {
                        bench::do_not_optimize(*v);
                        steal_count.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }

        const auto t_start = std::chrono::steady_clock::now();
        start_thieves.store(true, std::memory_order_release);

        for (std::size_t s = 0; s < ITEMS_LIFO; s += BATCH_PER_PROD) {
            const std::size_t end = std::min<std::size_t>(s + BATCH_PER_PROD, ITEMS_LIFO);
            const std::size_t cnt = end - s;
            const auto t0 = bench::rdtsc_start();
            for (std::size_t k = 0; k < cnt; ++k) {
                while (!owner.try_push(static_cast<std::uint64_t>(s + k))) {
                    // Owner pops to make room — thieves should drain but
                    // owner-pop is the cache-hot path.
                    (void)owner.try_pop();
                }
            }
            const auto t1 = bench::rdtsc_end();
            const std::uint64_t raw = t1 - t0;
            const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
            per_thread_ns[0].push_back(
                (static_cast<double>(adj) * nspc) / static_cast<double>(cnt));
        }
        // Drain whatever's left on the owner side.
        while (owner.try_pop()) { /* empty */ }

        stop_thieves.store(true, std::memory_order_release);
        for (auto& t : thieves) t.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += ITEMS_LIFO;
    }

    return aggregate_batched_(std::move(per_thread_ns), total_wall_ms, total_items);
}

template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_lifo_(
    const char* name, Channel& deq)
{
    using Ch = Channel;

    // Owner permission minted ONCE; owner handle long-lived across
    // iters.  Queue is empty between iters because the body drains
    // before signalling thieves to stop.
    auto owner_perm = mint_permission_root<typename Ch::owner_tag>();
    auto owner = deq.owner(std::move(owner_perm));

    return bench::Run{name}
        .samples(THRU_SAMPLES).warmup(THRU_WARMUP).batch(1).no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{
            std::atomic<bool> start_thieves{false};
            std::atomic<bool> stop_thieves{false};

            std::vector<std::jthread> thieves;
            for (std::size_t t = 0; t < NUM_THIEVES; ++t) {
                thieves.emplace_back([&](std::stop_token) noexcept {
                    auto handle = must_lend_(deq.thief(), "lifo thief (thru)");
                    while (!start_thieves.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    while (!stop_thieves.load(std::memory_order_acquire)) {
                        if (auto v = handle.try_steal()) {
                            bench::do_not_optimize(*v);
                        } else {
                            std::this_thread::yield();
                        }
                    }
                });
            }
            start_thieves.store(true, std::memory_order_release);
            for (std::size_t s = 0; s < ITEMS_LIFO; ++s) {
                while (!owner.try_push(static_cast<std::uint64_t>(s))) {
                    (void)owner.try_pop();
                }
            }
            while (owner.try_pop()) { /* drain */ }
            stop_thieves.store(true, std::memory_order_release);
            for (auto& t : thieves) t.join();
        });
}

// ═════════════════════════════════════════════════════════════════
// Runner: Permissioned ShardedGrid (LocalityAware).  4×4 grid.
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_sharded_(const char* name, Channel& grid) {
    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    // Hold one producer + ALL 4 consumers — RoundRobinRouting sends
    // p0's pushes round-robin across consumer columns, so we must
    // drain all 4 to keep cell depth bounded.  Bench body cost =
    // 1 try_push + 4 try_pop (3 of which return empty).
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));
    SimpleJob j{0};
    return bench::run(name, [&]{
        const bool ok = p0.try_push(j);
        bench::do_not_optimize(ok);
        auto r0 = c0.try_pop(); bench::do_not_optimize(r0);
        auto r1 = c1.try_pop(); bench::do_not_optimize(r1);
        auto r2 = c2.try_pop(); bench::do_not_optimize(r2);
        auto r3 = c3.try_pop(); bench::do_not_optimize(r3);
        ++j.v;
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_sharded_(
    Channel& grid, const PinningLayout& layout)
{
    constexpr std::size_t TOTAL_PER_ITER = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;
    constexpr std::size_t batches_per_prod =
        (ITEMS_FIFO_PER_PROD + BATCH_PER_PROD - 1) / BATCH_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    std::vector<std::vector<double>> per_prod_ns(NUM_PRODUCERS);
    for (auto& v : per_prod_ns) v.reserve(batches_per_prod * CONTENDED_ITERATIONS);

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>        start{false};
        std::atomic<std::size_t> consumed{0};

        auto run_producer = [&](auto& handle, std::size_t pid, int cpu) {
            return [&, pid, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                const double      nspc = bench::Timer::ns_per_cycle();
                const std::uint64_t ovh = bench::Timer::overhead_cycles();
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; s += BATCH_PER_PROD) {
                    const std::size_t end = std::min<std::size_t>(
                        s + BATCH_PER_PROD, ITEMS_FIFO_PER_PROD);
                    const std::size_t cnt = end - s;
                    const auto t0 = bench::rdtsc_start();
                    for (std::size_t k = 0; k < cnt; ++k) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | (s + k))};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                    const auto t1 = bench::rdtsc_end();
                    const std::uint64_t raw = t1 - t0;
                    const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
                    per_prod_ns[pid].push_back(
                        (static_cast<double>(adj) * nspc) / static_cast<double>(cnt));
                }
            };
        };
        auto run_consumer = [&](auto& handle, int cpu) {
            return [&, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                while (consumed.load(std::memory_order_acquire) < TOTAL_PER_ITER) {
                    if (auto opt = handle.try_pop()) {
                        bench::do_not_optimize(opt->v);
                        consumed.fetch_add(1, std::memory_order_acq_rel);
                    } else { std::this_thread::yield(); }
                }
            };
        };

        std::jthread t_p0(run_producer(p0, 0, layout.producer_cpu[0]));
        std::jthread t_p1(run_producer(p1, 1, layout.producer_cpu[1]));
        std::jthread t_p2(run_producer(p2, 2, layout.producer_cpu[2]));
        std::jthread t_p3(run_producer(p3, 3, layout.producer_cpu[3]));
        std::jthread t_c0(run_consumer(c0, layout.consumer_cpu[0]));
        std::jthread t_c1(run_consumer(c1, layout.consumer_cpu[1]));
        std::jthread t_c2(run_consumer(c2, layout.consumer_cpu[2]));
        std::jthread t_c3(run_consumer(c3, layout.consumer_cpu[3]));

        const auto t_start = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
        t_c0.join(); t_c1.join(); t_c2.join(); t_c3.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += TOTAL_PER_ITER;
    }

    return aggregate_batched_(std::move(per_prod_ns), total_wall_ms, total_items);
}

template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_sharded_(
    const char* name, Channel& grid)
{
    using Ch = Channel;
    using WT = typename Ch::whole_tag;
    constexpr std::size_t TOTAL = NUM_PRODUCERS * ITEMS_FIFO_PER_PROD;

    // Permissions minted once and split into the 4×4 grid; handles
    // long-lived across all bench iterations (queue is empty between
    // iters because each iter drains exactly TOTAL items).
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    return bench::Run{name}
        .samples(THRU_SAMPLES).warmup(THRU_WARMUP).batch(1).no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{
            std::atomic<bool>           start{false};
            std::atomic<std::size_t>    consumed{0};

            auto run_producer = [&](auto& handle, std::size_t pid) {
                return [&, pid](std::stop_token) noexcept {
                    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                    for (std::size_t s = 0; s < ITEMS_FIFO_PER_PROD; ++s) {
                        SimpleJob j{static_cast<std::uint64_t>(pid << 32 | s)};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                };
            };
            auto run_consumer = [&](auto& handle) {
                return [&](std::stop_token) noexcept {
                    while (consumed.load(std::memory_order_acquire) < TOTAL) {
                        if (auto opt = handle.try_pop()) {
                            bench::do_not_optimize(opt->v);
                            consumed.fetch_add(1, std::memory_order_acq_rel);
                        } else { std::this_thread::yield(); }
                    }
                };
            };

            std::jthread t_p0(run_producer(p0, 0));
            std::jthread t_p1(run_producer(p1, 1));
            std::jthread t_p2(run_producer(p2, 2));
            std::jthread t_p3(run_producer(p3, 3));
            std::jthread t_c0(run_consumer(c0));
            std::jthread t_c1(run_consumer(c1));
            std::jthread t_c2(run_consumer(c2));
            std::jthread t_c3(run_consumer(c3));
            start.store(true, std::memory_order_release);
            t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
            t_c0.join(); t_c1.join(); t_c2.join(); t_c3.join();
        });
}

// ═════════════════════════════════════════════════════════════════
// Runner: Permissioned CalendarGrid (Deadline / Cfs / Eevdf).
// Single consumer, M=4 static producers.  Floor uses one producer
// + one consumer.  Contended uses 4 producers + 1 consumer.
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_calendar_(const char* name, Channel& grid) {
    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));
    std::uint32_t s = 0;
    std::uint64_t key = 1000;
    return bench::run(name, [&]{
        ++s;
        key += 1000;
        PriorityJob j{0, s, key};
        if (!p0.try_push(j)) (void)cons.try_pop();
        (void)p0.try_push(j);
        auto popped = cons.try_pop();
        bench::do_not_optimize(popped);
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_calendar_(
    Channel& grid, const PinningLayout& layout)
{
    constexpr std::size_t TOTAL_PER_ITER = NUM_PRODUCERS * ITEMS_PRIORITY_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    constexpr std::size_t batches_per_prod =
        (ITEMS_PRIORITY_PER_PROD + BATCH_PER_PROD - 1) / BATCH_PER_PROD;
    std::vector<std::vector<double>> per_prod_ns(NUM_PRODUCERS);
    for (auto& v : per_prod_ns) v.reserve(batches_per_prod * CONTENDED_ITERATIONS);

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>        start{false};
        std::atomic<std::size_t> consumed{0};

        auto run_producer = [&](auto& handle, std::uint32_t pid, int cpu) {
            return [&, pid, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                const double      nspc = bench::Timer::ns_per_cycle();
                const std::uint64_t ovh = bench::Timer::overhead_cycles();
                std::uint64_t key = pid * 100ULL;
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (std::uint32_t s = 1; s <= ITEMS_PRIORITY_PER_PROD;
                     s += BATCH_PER_PROD)
                {
                    const std::uint32_t end = std::min<std::uint32_t>(
                        s + BATCH_PER_PROD, ITEMS_PRIORITY_PER_PROD + 1);
                    const std::uint32_t batch_cnt = end - s;
                    const auto t0 = bench::rdtsc_start();
                    for (std::uint32_t k = 0; k < batch_cnt; ++k) {
                        key += 1000;
                        PriorityJob j{pid, s + k, key};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                    const auto t1 = bench::rdtsc_end();
                    const std::uint64_t raw = t1 - t0;
                    const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
                    const double per_op_ns =
                        (static_cast<double>(adj) * nspc)
                        / static_cast<double>(batch_cnt);
                    per_prod_ns[pid].push_back(per_op_ns);
                }
            };
        };

        std::jthread t_p0(run_producer(p0, 0, layout.producer_cpu[0]));
        std::jthread t_p1(run_producer(p1, 1, layout.producer_cpu[1]));
        std::jthread t_p2(run_producer(p2, 2, layout.producer_cpu[2]));
        std::jthread t_p3(run_producer(p3, 3, layout.producer_cpu[3]));

        // Single consumer drains all 4 producers — pin to consumer<0>'s cpu.
        std::jthread cons_t([&, cpu = layout.consumer_cpu[0]](std::stop_token) noexcept {
            (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
            while (consumed.load(std::memory_order_acquire) < TOTAL_PER_ITER) {
                if (auto opt = cons.try_pop()) {
                    bench::do_not_optimize(opt->key);
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                } else { std::this_thread::yield(); }
            }
        });

        const auto t_start = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
        cons_t.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += TOTAL_PER_ITER;
    }

    return aggregate_batched_(std::move(per_prod_ns), total_wall_ms, total_items);
}

template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_calendar_(
    const char* name, Channel& grid)
{
    using Ch = Channel;
    using WT = typename Ch::whole_tag;
    constexpr std::size_t TOTAL = NUM_PRODUCERS * ITEMS_PRIORITY_PER_PROD;

    // Permissions minted ONCE — calendar grid is ~13 MB; per-iter
    // factory() would dominate the measurement (3-10× the actual
    // workload).  Long-lived handles, queue empty between iters.
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 1>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    return bench::Run{name}
        .samples(THRU_SAMPLES).warmup(THRU_WARMUP).batch(1).no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{
            std::atomic<bool>           start{false};
            std::atomic<std::size_t>    consumed{0};

            auto run_producer = [&](auto& handle, std::uint32_t pid) {
                return [&, pid](std::stop_token) noexcept {
                    std::uint64_t key = pid * 100ULL;
                    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                    for (std::uint32_t s = 1; s <= ITEMS_PRIORITY_PER_PROD; ++s) {
                        key += 1000;
                        PriorityJob j{pid, s, key};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                };
            };

            std::jthread t_p0(run_producer(p0, 0));
            std::jthread t_p1(run_producer(p1, 1));
            std::jthread t_p2(run_producer(p2, 2));
            std::jthread t_p3(run_producer(p3, 3));
            std::jthread cons_t([&](std::stop_token) noexcept {
                while (consumed.load(std::memory_order_acquire) < TOTAL) {
                    if (auto opt = cons.try_pop()) {
                        bench::do_not_optimize(opt->key);
                        consumed.fetch_add(1, std::memory_order_acq_rel);
                    } else { std::this_thread::yield(); }
                }
            });
            start.store(true, std::memory_order_release);
            t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
            cons_t.join();
        });
}

// ═════════════════════════════════════════════════════════════════
// Runner: PermissionedShardedCalendarGrid (DeadlinePerShard /
// CfsPerShard / EevdfPerShard).  4 producer<S> + 4 consumer<S>
// pairs — each pair owns one shard end-to-end.  This is the
// per-shard architecture's intended deployment shape.
// ═════════════════════════════════════════════════════════════════

template <typename Channel>
[[nodiscard]] bench::Report bench_floor_per_shard_(
    const char* name, Channel& grid)
{
    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    std::uint32_t s = 0;
    std::uint64_t key = 1000;
    return bench::run(name, [&]{
        ++s;
        key += 1000;
        PriorityJob j{0, s, key};
        if (!p0.try_push(j)) (void)c0.try_pop();
        (void)p0.try_push(j);
        auto popped = c0.try_pop();
        bench::do_not_optimize(popped);
    });
}

template <typename Channel>
[[nodiscard]] ContendedResult bench_contended_per_shard_(
    Channel& grid, const PinningLayout& layout)
{
    constexpr std::size_t TOTAL_PER_ITER = NUM_PRODUCERS * ITEMS_PRIORITY_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    constexpr std::size_t batches_per_prod =
        (ITEMS_PRIORITY_PER_PROD + BATCH_PER_PROD - 1) / BATCH_PER_PROD;
    std::vector<std::vector<double>> per_prod_ns(NUM_PRODUCERS);
    for (auto& v : per_prod_ns) v.reserve(batches_per_prod * CONTENDED_ITERATIONS);

    double      total_wall_ms = 0.0;
    std::size_t total_items   = 0;

    for (std::size_t iter = 0; iter < CONTENDED_ITERATIONS; ++iter) {
        std::atomic<bool>                       start{false};
        std::array<std::atomic<std::size_t>, 4> consumed_per_shard{};

        auto run_producer = [&](auto& handle, std::uint32_t pid, int cpu) {
            return [&, pid, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                const double      nspc = bench::Timer::ns_per_cycle();
                const std::uint64_t ovh = bench::Timer::overhead_cycles();
                std::uint64_t key = pid * 100ULL;
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (std::uint32_t s = 1; s <= ITEMS_PRIORITY_PER_PROD;
                     s += BATCH_PER_PROD)
                {
                    const std::uint32_t end = std::min<std::uint32_t>(
                        s + BATCH_PER_PROD, ITEMS_PRIORITY_PER_PROD + 1);
                    const std::uint32_t batch_cnt = end - s;
                    const auto t0 = bench::rdtsc_start();
                    for (std::uint32_t k = 0; k < batch_cnt; ++k) {
                        key += 1000;
                        PriorityJob j{pid, s + k, key};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                    const auto t1 = bench::rdtsc_end();
                    const std::uint64_t raw = t1 - t0;
                    const std::uint64_t adj = (raw > ovh) ? (raw - ovh) : 0;
                    const double per_op_ns =
                        (static_cast<double>(adj) * nspc)
                        / static_cast<double>(batch_cnt);
                    per_prod_ns[pid].push_back(per_op_ns);
                }
            };
        };

        auto run_consumer = [&](auto& handle, std::size_t shard_idx, int cpu) {
            return [&, shard_idx, cpu](std::stop_token) noexcept {
                (void)pin_thread_to_cpu_(std::this_thread::get_id(), cpu);
                while (consumed_per_shard[shard_idx].load(std::memory_order_acquire)
                       < ITEMS_PRIORITY_PER_PROD)
                {
                    if (auto opt = handle.try_pop()) {
                        bench::do_not_optimize(opt->key);
                        consumed_per_shard[shard_idx].fetch_add(
                            1, std::memory_order_acq_rel);
                    } else {
                        std::this_thread::yield();
                    }
                }
            };
        };

        std::jthread t_p0(run_producer(p0, 0, layout.producer_cpu[0]));
        std::jthread t_p1(run_producer(p1, 1, layout.producer_cpu[1]));
        std::jthread t_p2(run_producer(p2, 2, layout.producer_cpu[2]));
        std::jthread t_p3(run_producer(p3, 3, layout.producer_cpu[3]));
        std::jthread t_c0(run_consumer(c0, 0, layout.consumer_cpu[0]));
        std::jthread t_c1(run_consumer(c1, 1, layout.consumer_cpu[1]));
        std::jthread t_c2(run_consumer(c2, 2, layout.consumer_cpu[2]));
        std::jthread t_c3(run_consumer(c3, 3, layout.consumer_cpu[3]));

        const auto t_start = std::chrono::steady_clock::now();
        start.store(true, std::memory_order_release);
        t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
        t_c0.join(); t_c1.join(); t_c2.join(); t_c3.join();
        const auto t_end = std::chrono::steady_clock::now();
        total_wall_ms += std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        total_items   += TOTAL_PER_ITER;
    }

    return aggregate_batched_(std::move(per_prod_ns), total_wall_ms, total_items);
}

template <typename Channel>
[[nodiscard]] bench::Report bench_throughput_per_shard_(
    const char* name, Channel& grid)
{
    using WT = typename Channel::whole_tag;
    constexpr std::size_t TOTAL = NUM_PRODUCERS * ITEMS_PRIORITY_PER_PROD;

    auto whole = mint_permission_root<WT>();
    auto perms = split_grid<WT, 4, 4>(std::move(whole));
    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    return bench::Run{name}
        .samples(THRU_SAMPLES).warmup(THRU_WARMUP).batch(1).no_pin()
        .max_wall_ms(THRU_MAX_WALL_MS)
        .measure([&]{
            std::atomic<bool>                       start{false};
            std::array<std::atomic<std::size_t>, 4> consumed_per_shard{};

            auto run_producer = [&](auto& handle, std::uint32_t pid) {
                return [&, pid](std::stop_token) noexcept {
                    std::uint64_t key = pid * 100ULL;
                    while (!start.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    for (std::uint32_t s = 1; s <= ITEMS_PRIORITY_PER_PROD; ++s) {
                        key += 1000;
                        PriorityJob j{pid, s, key};
                        while (!handle.try_push(j)) std::this_thread::yield();
                    }
                };
            };

            auto run_consumer = [&](auto& handle, std::size_t shard_idx) {
                return [&, shard_idx](std::stop_token) noexcept {
                    while (consumed_per_shard[shard_idx].load(std::memory_order_acquire)
                           < ITEMS_PRIORITY_PER_PROD)
                    {
                        if (auto opt = handle.try_pop()) {
                            bench::do_not_optimize(opt->key);
                            consumed_per_shard[shard_idx].fetch_add(
                                1, std::memory_order_acq_rel);
                        } else {
                            std::this_thread::yield();
                        }
                    }
                };
            };

            std::jthread t_p0(run_producer(p0, 0));
            std::jthread t_p1(run_producer(p1, 1));
            std::jthread t_p2(run_producer(p2, 2));
            std::jthread t_p3(run_producer(p3, 3));
            std::jthread t_c0(run_consumer(c0, 0));
            std::jthread t_c1(run_consumer(c1, 1));
            std::jthread t_c2(run_consumer(c2, 2));
            std::jthread t_c3(run_consumer(c3, 3));
            start.store(true, std::memory_order_release);
            t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
            t_c0.join(); t_c1.join(); t_c2.join(); t_c3.join();

            // Workload only counts to TOTAL via per-shard sum; this
            // is informational so the harness output matches the
            // semantic of "items processed".
            (void)TOTAL;
        });
}

}  // namespace

// ═════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    std::printf("=== scheduler_policies (3-axis) ===\n");
    std::printf("  NUM_PRODUCERS = %zu  NUM_CONSUMERS = %zu  NUM_THIEVES = %zu\n",
                NUM_PRODUCERS, NUM_CONSUMERS, NUM_THIEVES);
    std::printf("  Items per cycle: FIFO/MPSC/Sharded = 4×%zu = %zu\n",
                ITEMS_FIFO_PER_PROD, NUM_PRODUCERS * ITEMS_FIFO_PER_PROD);
    std::printf("                   Lifo (owner) = %zu\n", ITEMS_LIFO);
    std::printf("                   Priority (Cal grid) = 4×%zu = %zu\n",
                ITEMS_PRIORITY_PER_PROD, NUM_PRODUCERS * ITEMS_PRIORITY_PER_PROD);
    std::printf("  THRU_SAMPLES = %zu  warmup = %zu  max_wall = %zu ms\n",
                THRU_SAMPLES, THRU_WARMUP, THRU_MAX_WALL_MS);
    std::printf("  rdtsc resolution: %.2f ns (overhead = %lu cycles, "
                "ns/cyc = %.4f)\n",
                bench::Timer::overhead_ns(),
                static_cast<unsigned long>(bench::Timer::overhead_cycles()),
                bench::Timer::ns_per_cycle());
    std::printf("  contended-tail measurement: BATCH_PER_PROD = %u "
                "pushes per rdtsc bracket → per-op cost amortized "
                "above the rdtsc resolution floor\n",
                BATCH_PER_PROD);

    const PinningLayout layout = choose_layout_();
    print_pinning_layout_(layout);
    std::printf("\n");

    std::vector<PolicyResults> results;

    // ── Fifo ──────────────────────────────────────────────────────
    //
    // Each policy uses THREE distinct channel instances — one per
    // measurement axis.  Floor leaves a long-lived producer/consumer
    // bound to the channel; contended creates threads that lend their
    // own handles; throughput needs a fresh channel because its
    // hoisted handles must be created fresh (the channel's
    // permission-tree state is single-issue).

    {
        std::printf("[Fifo] benching ...\n");
        using QT = cs::Fifo::queue_template<SimpleJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_pmpmc_("Fifo floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_pmpmc_(*ch_tail, layout);
        results.push_back({"Fifo", std::move(floor), std::move(tail)});
    }

    // ── RoundRobin ────────────────────────────────────────────────
    {
        std::printf("[RoundRobin] benching ...\n");
        using QT = cs::RoundRobin::queue_template<SimpleJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_pmpsc_("RoundRobin floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_pmpsc_(*ch_tail, layout);
        results.push_back({"RoundRobin", std::move(floor), std::move(tail)});
    }

    // ── Lifo (ChaseLevDeque) ─────────────────────────────────────
    {
        std::printf("[Lifo] benching ...\n");
        using QT = cs::Lifo::queue_template<std::uint64_t>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_lifo_("Lifo floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_lifo_(*ch_tail, layout);
        results.push_back({"Lifo", std::move(floor), std::move(tail)});
    }

    // ── LocalityAware (4×4 ShardedGrid) ──────────────────────────
    {
        std::printf("[LocalityAware] benching ...\n");
        using QT = cs::LocalityAware::queue_template<SimpleJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_sharded_("LocalityAware floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_sharded_(*ch_tail, layout);
        results.push_back({"LocalityAware", std::move(floor), std::move(tail)});
    }

    // ── Deadline / Cfs / Eevdf (single-grid calendar) ────────────
    {
        std::printf("[Deadline] benching ...\n");
        using QT = SchedDeadline::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_calendar_("Deadline floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_calendar_(*ch_tail, layout);
        results.push_back({"Deadline", std::move(floor), std::move(tail)});
    }
    {
        std::printf("[Cfs] benching ...\n");
        using QT = SchedCfs::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_calendar_("Cfs floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_calendar_(*ch_tail, layout);
        results.push_back({"Cfs", std::move(floor), std::move(tail)});
    }
    {
        std::printf("[Eevdf] benching ...\n");
        using QT = SchedEevdf::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_calendar_("Eevdf floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_calendar_(*ch_tail, layout);
        results.push_back({"Eevdf", std::move(floor), std::move(tail)});
    }

    // ── DeadlinePerShard / CfsPerShard / EevdfPerShard ───────────
    //
    // Per-shard architecture: 4 producer threads each pushing to
    // their own shard, 4 consumer threads each draining their own
    // shard.  No cross-thread atomic on the producer push path.
    {
        std::printf("[DeadlinePerShard] benching ...\n");
        using QT = SchedDeadlinePerShard::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_per_shard_("DeadlinePerShard floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_per_shard_(*ch_tail, layout);
        results.push_back({"DeadlinePerShard", std::move(floor), std::move(tail)});
    }
    {
        std::printf("[CfsPerShard] benching ...\n");
        using QT = SchedCfsPerShard::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_per_shard_("CfsPerShard floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_per_shard_(*ch_tail, layout);
        results.push_back({"CfsPerShard", std::move(floor), std::move(tail)});
    }
    {
        std::printf("[EevdfPerShard] benching ...\n");
        using QT = SchedEevdfPerShard::queue_template<PriorityJob>;
        auto ch_floor = std::make_unique<QT>();
        auto floor = bench_floor_per_shard_("EevdfPerShard floor (push+pop)", *ch_floor);
        auto ch_tail = std::make_unique<QT>();
        auto tail  = bench_contended_per_shard_(*ch_tail, layout);
        results.push_back({"EevdfPerShard", std::move(floor), std::move(tail)});
    }

    // ─── Print headline table ────────────────────────────────────
    std::printf("\n=== HEADLINE: latency + steady-state throughput ===\n");
    std::printf("  floor:        single-thread per-op (push+pop), ns\n");
    std::printf("  tail:         per-submit latency under N=%zu producer contention, ns\n",
                NUM_PRODUCERS);
    std::printf("  steady-state: items processed / wall time, aggregated\n");
    std::printf("                across %zu contended iterations.  This is the\n",
                CONTENDED_ITERATIONS);
    std::printf("                production-shape number — long-lived workers,\n");
    std::printf("                no per-iter pthread_create overhead.\n");
    print_policy_table_header_();
    for (auto const& r : results) print_policy_row_(r);

    std::printf("\n=== Steady-state composition (raw) ===\n");
    std::printf("%-18s | %12s %14s %14s\n",
                "policy", "total items", "total wall ms", "items/s (M)");
    for (auto const& r : results) {
        std::printf("%-18s | %12zu %14.3f %14.2f\n",
                    r.policy_name,
                    r.tail.total_items,
                    r.tail.total_wall_ms,
                    r.tail.items_per_sec / 1e6);
    }

    return 0;
}
