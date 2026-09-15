#pragma once

// What three probes turned out to need in common.
//
// This header was not designed ahead of the probes. #68 (vector width),
// #69 (cache-tier knee and NUMA hop) and #70 (transparent-hugepage
// fault cost) were each written against the bench harness first, and
// what all three of them did the same way was lifted here afterwards.
// Anything only one of them needed stayed with that one. The list came
// out at five items:
//
//   1. Two knobs — how many samples, which core. The ProbeFunction
//      signature in Ledger.h carries only a CompetenceReport, on
//      purpose, so these reach a probe through a settings block rather
//      than through an argument.
//
//   2. Two runs folded into one VerdictEvidence. Every probe runs its
//      measurement twice and reports the better value against the worse
//      statistics, because within-run spread and run-to-run spread fail
//      for different reasons and neither substitutes for the other.
//
//   3. An A/B verdict. All three probes answer a comparison — 512 against
//      256, remote against local, huge pages against base pages — and all
//      three have to be able to say "the same" when the two are the same.
//
//   4. A scratch region whose page policy the probe chooses. #69 needs
//      base pages so the cache curve is not confounded by TLB coverage;
//      #70 needs both policies because the difference IS the measurement.
//
//   5. Thread pinning for a helper. #69 is the only probe that spawns
//      one, but it spawns three, and the affinity call belongs next to
//      the rest of the measurement plumbing rather than inside a probe.
//
// The A/B rule, stated once so all three probes apply it identically:
// a difference counts only when it is BOTH statistically distinguishable
// (Mann-Whitney U at p<0.01, which the bench harness already computes)
// AND at least kPracticalMarginPercent wide. The conjunction is
// load-bearing in both directions. At fifty thousand samples U calls a
// 0.15% difference distinguishable, which is true and useless. And a
// 20% difference over eight samples is wide but unsupported. A probe
// that reports "no preference" because neither test passed has given a
// real answer, and its conservative reading is named at the call site
// like every other.
//
// DetSafe (axiom 8): nothing here is reachable from content_hash,
// merkle_hash or the memory plan. A probe measures how fast the host is
// and never what it computes.

#include <crucible/Platform.h>
#include <crucible/ledger/Ledger.h>
#include <crucible/safety/OwnedMmap.h>

#include <bench_harness.h>

#include <sched.h>
#include <sys/mman.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::ledger {

// ── Builds that cannot measure hardware ───────────────────────────────
//
// A sanitizer instruments memory accesses, and it instruments them PER
// ACCESS rather than per byte. A 512-bit load and a 256-bit load each
// cost one shadow check, so a kernel that moves the same bytes in half as
// many instructions comes out twice as fast — for a reason that has
// nothing to do with the silicon.
//
// This is not hypothetical. The vector-width probe's streaming shape
// reports 101% in an ordinary build of the development host, meaning the
// two widths tie because the bottleneck is DRAM. The same probe under
// AddressSanitizer reported 190%, meaning the wide width nearly doubled
// the streaming throughput. The second number is a measurement of
// AddressSanitizer.
//
// So an instrumented build declines to measure rather than producing
// numbers about its own instrumentation. Every probe checks this first
// and answers NotApplicableOnThisHost, which is the same answer a host
// with no wide vector unit gives and travels the same fail-closed path.
//
// ThreadSanitizer is included for the same reason and MemorySanitizer
// would be if GCC had it. UndefinedBehaviorSanitizer is deliberately NOT
// included: GCC advertises no macro for it, and what it instruments —
// arithmetic overflow, bounds on real array subscripts — is not what
// these kernels do, so its cost does not scale with access width.

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
inline constexpr bool kBuildIsInstrumented = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
inline constexpr bool kBuildIsInstrumented = true;
#else
inline constexpr bool kBuildIsInstrumented = false;
#endif
#else
inline constexpr bool kBuildIsInstrumented = false;
#endif

// ── Measurement parameters ────────────────────────────────────────────
//
// A probe is a plain function pointer taking one argument, so the two
// things a measurement genuinely needs from its invoker arrive here.
// The alternative — widening ProbeFunction — would put a sample count in
// the signature of every future probe including the ones that do not
// take samples, and would make the registration table stop being a
// compile-time constant a reader can follow.
//
// constinit at namespace scope rather than a function-local static: a
// function-local would emit a guard variable and a first-call branch,
// and the house rule bans dynamically-initialized globals outright. The
// aggregate is trivially constructible, so this is one zero-filled
// object in .bss with no initializer to order.

inline constexpr std::size_t kMaxHelperCores = 4;

struct ProbeSettings {
    // Samples per run. Each probe runs twice, so the evidence's sample
    // count is this, not twice this.
    std::uint32_t sample_count = 4096;

    // The core the measurement runs on. Negative asks the bench harness
    // to pick, which prefers an isolated CPU and falls back to the
    // current one.
    int pin_core = -1;

    // Cores for helper threads, used only by the probe that splits work
    // across cores. A negative entry ends the list.
    std::array<int, kMaxHelperCores> helper_cores{-1, -1, -1, -1};

    [[nodiscard]] constexpr std::uint32_t helper_count() const noexcept {
        std::uint32_t found = 0;
        for (const int core : helper_cores) {
            if (core < 0) {
                break;
            }
            ++found;
        }
        return found;
    }
};

static_assert(std::is_trivially_copyable_v<ProbeSettings>);

inline constinit ProbeSettings g_probe_settings{};

[[nodiscard]] inline ProbeSettings probe_settings() noexcept { return g_probe_settings; }

inline void set_probe_settings(ProbeSettings settings) noexcept { g_probe_settings = settings; }

// ── Evidence from bench reports ───────────────────────────────────────

// Rounds up. A floor of zero reads as ZeroMedian at the admission, which
// is the right reading for a timer that did not run and the wrong one
// for a result below a nanosecond.
[[nodiscard]] inline std::uint32_t saturating_nanos(double value) noexcept {
    if (!(value > 0.0)) {
        return 0u;
    }
    const double rounded = value + 0.5;
    if (rounded >= 4294967295.0) {
        return 4294967295u;
    }
    const auto nanos = static_cast<std::uint32_t>(rounded);
    return (nanos == 0u) ? 1u : nanos;
}

[[nodiscard]] inline std::uint32_t to_ppm(double ratio) noexcept {
    if (!(ratio > 0.0)) {
        return 0u;
    }
    const double parts = ratio * 1'000'000.0;
    if (parts >= 4294967295.0) {
        return 4294967295u;
    }
    return static_cast<std::uint32_t>(parts);
}

// Percent of `baseline` that `candidate` represents, as a gain: 200 means
// the candidate took half the time, 100 means they tied, 50 means the
// candidate took twice as long. Saturates rather than wrapping, and
// answers zero when either input is unusable, which reads as a refusal
// downstream instead of as a tie.
[[nodiscard]] inline std::uint32_t gain_percent(double baseline_ns, double candidate_ns) noexcept {
    if (!(baseline_ns > 0.0) || !(candidate_ns > 0.0)) {
        return 0u;
    }
    const double percent = 100.0 * baseline_ns / candidate_ns;
    if (percent >= 4294967295.0) {
        return 4294967295u;
    }
    return static_cast<std::uint32_t>(percent + 0.5);
}

// The evidence record for a measurement taken twice.
//
// The reported median is the better of the two runs and the reported
// tails, sample count and within-run spread are the worse of the two.
// That pairing is deliberate and is the conservative one: a reader who
// trusts the value has already been shown the least flattering account
// of how it was obtained.
[[nodiscard]] inline VerdictEvidence evidence_from_two_runs(bench::Report const& first,
                                                            bench::Report const& second) noexcept {
    VerdictEvidence evidence{};
    if (first.pct.n == 0u || second.pct.n == 0u) {
        return evidence;  // audits as TooFewSamples, which is what it is
    }

    const double first_p50 = first.pct.p50;
    const double second_p50 = second.pct.p50;
    const double midpoint = (first_p50 + second_p50) * 0.5;
    const double spread =
        (midpoint > 0.0) ? (std::max(first_p50, second_p50) - std::min(first_p50, second_p50)) / midpoint : 1.0;

    bench::Report const& worse = (first.pct.cv >= second.pct.cv) ? first : second;

    evidence.quantiles.p50_ns = saturating_nanos(std::min(first_p50, second_p50));
    evidence.quantiles.p99_ns = saturating_nanos(worse.pct.p99);
    evidence.quantiles.p999_ns = saturating_nanos(worse.pct.p99_9);
    // Rounding a sub-nanosecond spread can flatten the triple or invert
    // it, because the median comes from the better run and the tails from
    // the worse one. The ordering predicate rejects an inverted triple, so
    // the tails are lifted to the median rather than failing an admission
    // over a rounding artefact.
    evidence.quantiles.p99_ns = std::max(evidence.quantiles.p99_ns, evidence.quantiles.p50_ns);
    evidence.quantiles.p999_ns = std::max(evidence.quantiles.p999_ns, evidence.quantiles.p99_ns);

    evidence.sample_count = static_cast<std::uint32_t>(std::min<std::size_t>(worse.pct.n, 0xFFFFFFFFull));
    evidence.within_run_cv_ppm = to_ppm(worse.pct.cv);
    evidence.run_to_run_spread_ppm = to_ppm(spread);
    return evidence;
}

// Relative distance between two values, in parts per million of their
// midpoint. The shape the evidence record wants for a run-to-run spread.
[[nodiscard]] inline std::uint32_t spread_ppm(double first, double second) noexcept {
    const double midpoint = (first + second) * 0.5;
    if (!(midpoint > 0.0)) {
        return 4294967295u;  // reads as RunToRunSpreadTooHigh, which is what it is
    }
    return to_ppm((std::max(first, second) - std::min(first, second)) / midpoint);
}

// Evidence for a verdict that is a RATIO of two measurements rather than
// one measurement.
//
// This exists because the obvious thing is wrong, and the wrong version
// shipped first. A gain verdict stored the evidence of the faster of its
// two variants, which described how steady that variant was and said
// nothing about whether the RATIO was reproducible. On the development
// host, under load from other work, the hugepage access-gain verdict came
// out at 160% on one run and 93% on the next, and both times the variant
// whose evidence was stored had a within-run coefficient of variation
// near one percent. Two contradictory answers, both certified steady.
//
// So the ratio is measured twice, from two independent A/B pairs, and it
// is the two RATIOS whose disagreement goes into the run-to-run field.
// The within-run field takes the worst of the four runs, because a
// comparison is no steadier than its least steady half. A ratio that does
// not reproduce is now refused at admission by the same bar that catches
// a throttling part, which is where it belonged all along.
[[nodiscard]] inline VerdictEvidence evidence_for_ratio(bench::Report const& baseline_first,
                                                        bench::Report const& candidate_first,
                                                        bench::Report const& baseline_second,
                                                        bench::Report const& candidate_second) noexcept {
    VerdictEvidence evidence{};
    if (baseline_first.pct.n == 0u || candidate_first.pct.n == 0u || baseline_second.pct.n == 0u
        || candidate_second.pct.n == 0u) {
        return evidence;  // audits as TooFewSamples
    }
    if (!(baseline_first.pct.p50 > 0.0) || !(candidate_first.pct.p50 > 0.0) || !(baseline_second.pct.p50 > 0.0)
        || !(candidate_second.pct.p50 > 0.0)) {
        return evidence;  // audits as ZeroMedian
    }

    const double first_ratio = baseline_first.pct.p50 / candidate_first.pct.p50;
    const double second_ratio = baseline_second.pct.p50 / candidate_second.pct.p50;

    // The quantiles describe the candidate, which is the side a reader
    // inspecting a gain verdict wants to see. The spreads describe the
    // ratio, which is what the verdict actually claims.
    evidence.quantiles.p50_ns = saturating_nanos(std::min(candidate_first.pct.p50, candidate_second.pct.p50));
    evidence.quantiles.p99_ns = saturating_nanos(std::max(candidate_first.pct.p99, candidate_second.pct.p99));
    evidence.quantiles.p999_ns = saturating_nanos(std::max(candidate_first.pct.p99_9, candidate_second.pct.p99_9));
    evidence.quantiles.p99_ns = std::max(evidence.quantiles.p99_ns, evidence.quantiles.p50_ns);
    evidence.quantiles.p999_ns = std::max(evidence.quantiles.p999_ns, evidence.quantiles.p99_ns);

    evidence.sample_count = static_cast<std::uint32_t>(
        std::min({baseline_first.pct.n, candidate_first.pct.n, baseline_second.pct.n, candidate_second.pct.n}));
    evidence.within_run_cv_ppm = to_ppm(
        std::max({baseline_first.pct.cv, candidate_first.pct.cv, baseline_second.pct.cv, candidate_second.pct.cv}));
    evidence.run_to_run_spread_ppm = spread_ppm(first_ratio, second_ratio);
    return evidence;
}

// ── The A/B rule ──────────────────────────────────────────────────────

// Below this, two variants are the same thing however many samples say
// otherwise. Five percent is the house variance bar: a difference no
// larger than the noise each side is allowed to carry is not a
// difference anyone can act on.
inline constexpr std::uint32_t kPracticalMarginPercent = 5;

struct VariantComparison {
    double baseline_p50_ns = 0;
    double candidate_p50_ns = 0;

    // 100 = tie. Above 100 the candidate is faster.
    std::uint32_t candidate_gain_percent = 0;

    // Mann-Whitney U said the two distributions differ at p<0.01.
    bool is_statistically_distinguishable = false;

    // The gap is at least kPracticalMarginPercent wide.
    bool is_practically_wide = false;

    // Both. The only reading a probe may act on.
    [[nodiscard]] constexpr bool candidate_wins() const noexcept {
        return is_statistically_distinguishable && is_practically_wide && candidate_gain_percent > 100u;
    }
    [[nodiscard]] constexpr bool is_a_tie() const noexcept {
        return !(is_statistically_distinguishable && is_practically_wide);
    }
};

[[nodiscard]] inline VariantComparison compare_variants(bench::Report const& baseline,
                                                        bench::Report const& candidate) noexcept {
    VariantComparison comparison{};
    comparison.baseline_p50_ns = baseline.pct.p50;
    comparison.candidate_p50_ns = candidate.pct.p50;
    comparison.candidate_gain_percent = gain_percent(baseline.pct.p50, candidate.pct.p50);

    // bench::compare allocates two rank vectors, which is why this is not
    // noexcept-by-construction; it is called once per probe on a cold
    // path and an allocation failure there aborts like any other.
    const bench::Compare ranked = bench::compare(baseline, candidate);
    comparison.is_statistically_distinguishable = ranked.distinguishable;

    if (comparison.candidate_gain_percent != 0u) {
        const std::uint32_t distance = (comparison.candidate_gain_percent >= 100u)
                                         ? (comparison.candidate_gain_percent - 100u)
                                         : (100u - comparison.candidate_gain_percent);
        comparison.is_practically_wide = distance >= kPracticalMarginPercent;
    }
    return comparison;
}

// ── Scratch memory ────────────────────────────────────────────────────

enum class PagePolicy : std::uint8_t {
    // Let the kernel decide. Not used by any probe: every probe states
    // its page policy, because a measurement that does not know its own
    // TLB coverage is not a measurement of what it claims.
    Default = 0,
    // MADV_NOHUGEPAGE. What a cache-geometry probe wants, so the curve
    // it draws is about the caches and not about how many pages the TLB
    // happened to cover.
    BasePages = 1,
    // MADV_HUGEPAGE.
    HugePages = 2,
};

namespace probe_detail {

struct ProbeRegionTag {};
struct ProbeRegionProt {};
struct ProbeRegionShare {};

using ProbeMapping = safety::OwnedMmap<ProbeRegionTag, ProbeRegionProt, ProbeRegionShare>;

}  // namespace probe_detail

// An anonymous scratch region, 2 MiB-aligned so MADV_HUGEPAGE can
// actually take, unmapped on destruction by OwnedMmap.
//
// The alignment is not decoration. A region that is not 2 MiB-aligned
// gets huge pages only for whatever whole 2 MiB windows happen to fall
// inside it, so a hugepage measurement over an unaligned region reports
// a blend of two page sizes and calls it one.
class [[nodiscard]] ProbeRegion {
public:
    ProbeRegion() noexcept = default;

    ProbeRegion(ProbeRegion const&) = delete("a scratch region is unique; a copy would double-unmap");
    ProbeRegion& operator=(ProbeRegion const&) = delete("a scratch region is unique; a copy would double-unmap");
    ProbeRegion(ProbeRegion&&) noexcept = default;
    ProbeRegion& operator=(ProbeRegion&&) noexcept = default;
    ~ProbeRegion() noexcept = default;

    // Returns a region of at least `bytes`, or the reason there is none.
    // A probe that cannot get memory reports StoreReadFailed rather than
    // measuring something smaller and not saying so.
    [[nodiscard]] static std::expected<ProbeRegion, LedgerError> create(std::size_t bytes, PagePolicy policy) noexcept {
        if (bytes == 0u) {
            return std::unexpected(LedgerError::MalformedRecord);
        }
        // One huge page of slack so the aligned start plus `bytes` still
        // lands inside the mapping whatever address the kernel picks.
        const std::size_t mapped_bytes = bytes + kHugePageBytes;

        // The capability proof: every caller is a ProbeFunction reached
        // from run_refresh, which the daemon and the tool both invoke
        // under a context satisfying CtxFitsLedgerStore — effects::IO
        // plus effects::Block, checked at the type level in LedgerStore.h
        // and witnessed by the neg-compile fixtures in test/ledger_neg/.
        void* raw = ::mmap(nullptr, mapped_bytes, PROT_READ | PROT_WRITE,  // SYSCALL-CAP-OK: see the proof above
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw == MAP_FAILED) {
            return std::unexpected(LedgerError::StorePathUnavailable);
        }

        ProbeRegion region{};
        region.mapping_ = probe_detail::ProbeMapping{raw, mapped_bytes};

        const auto address = align_up_to_huge_page(raw);
        region.usable_ = address;
        region.usable_bytes_ = bytes;

        const int advice = (policy == PagePolicy::HugePages) ? MADV_HUGEPAGE
                         : (policy == PagePolicy::BasePages) ? MADV_NOHUGEPAGE
                                                             : MADV_NORMAL;
        if (advice != MADV_NORMAL) {
            // Advisory by definition: a kernel built without transparent
            // hugepage support answers EINVAL and the region is still
            // usable, just at whatever page size the kernel chose. The
            // probe that cares reads back /proc/self/smaps to find out
            // what it actually got rather than trusting this call.
            (void)::madvise(address, bytes, advice);  // SYSCALL-CAP-OK: see the proof above
        }
        return region;
    }

    [[nodiscard]] void* data() const noexcept { return usable_; }
    [[nodiscard]] std::size_t size() const noexcept { return usable_bytes_; }
    [[nodiscard]] bool is_mapped() const noexcept { return mapping_.is_mapped() && usable_ != nullptr; }

    // Writes to every page so the measurement that follows is not timing
    // page faults. Returns the number of bytes touched.
    std::size_t fault_in(std::uint32_t byte_value = 1u) noexcept {
        if (!is_mapped()) {
            return 0u;
        }
        auto* bytes = static_cast<unsigned char*>(usable_);
        for (std::size_t offset = 0; offset < usable_bytes_; offset += kBasePageBytes) {
            bytes[offset] = static_cast<unsigned char>(byte_value);
        }
        return usable_bytes_;
    }

    static constexpr std::size_t kBasePageBytes = 4096;
    static constexpr std::size_t kHugePageBytes = 2u * 1024u * 1024u;

private:
    // Rounds an address up to the next huge-page boundary. The
    // arithmetic has to happen on an integer, and reinterpret_cast is
    // banned tree-wide, so the retyping goes through std::bit_cast.
    [[nodiscard]] static void* align_up_to_huge_page(void* raw) noexcept {
        const auto address = std::bit_cast<std::uintptr_t>(raw);
        constexpr std::uintptr_t mask = static_cast<std::uintptr_t>(kHugePageBytes) - 1u;
        const std::uintptr_t aligned = (address + mask) & ~mask;
        return std::bit_cast<void*>(aligned);
    }

    probe_detail::ProbeMapping mapping_{};
    void* usable_ = nullptr;
    std::size_t usable_bytes_ = 0;
};

// ── Sharing one measurement between several verdicts ──────────────────
//
// Two of the three probes answer more than one verdict from a single
// measurement: the vector-width probe produces a preferred width and the
// two margins it derived that width from, and the cache probe produces a
// knee and a ceiling from one sweep. The registry maps one id to one
// function, so without a memo each of those would re-run the whole
// measurement and the sibling verdicts would disagree about a host that
// had not changed in between.
//
// The memo is a cache with a time to live, which is the same shape as the
// ledger itself one level down, and it expires in seconds rather than
// hours. Inside one refresh cycle every sibling reads one measurement.
// Across cycles the measurement is taken again.
//
// It is NOT thread-safe and does not need to be. run_refresh is called
// from the tool's main thread or from the single refresh thread in
// RefreshDaemon.h, never from two at once, and no path in the tree calls
// a ProbeFunction from anywhere else.
//
// The lifetime is bounded on both sides and the bounds are not arbitrary.
//
//   Long enough to outlive one refresh cycle, or the memo expires between
//   two siblings and the second re-runs the whole measurement. That is
//   not merely slow: the re-run happens under the load the first run just
//   created, so the sibling verdicts end up describing a machine in two
//   different states and neither of them the idle one. It was set to five
//   seconds first, and on the development host the hugepage measurement
//   takes about twenty, so all three of its verdicts measured separately
//   and disagreed — one reported a fault cost of 40.3 microseconds per
//   mebibyte and its sibling, measuring the same thing forty seconds
//   later, reported 43.7.
//
//   Shorter than the shortest time to live in the ledger, or a cycle
//   could serve a verdict the ledger has already decided is stale. Two
//   minutes against the fifteen the hugepage verdicts get, asserted below
//   so the two cannot drift into agreement by accident.
inline constexpr std::uint64_t kMemoLifetimeSeconds = 120;
inline constexpr std::uint64_t kMemoLifetimeNanos = kMemoLifetimeSeconds * 1000ull * 1000ull * 1000ull;

static_assert(kMemoLifetimeSeconds < kFragmentationTtlSeconds,
              "a memo that outlives the shortest TTL could serve a verdict the ledger calls stale");

[[nodiscard]] inline std::uint64_t monotonic_nanos() noexcept {
    const auto since_epoch = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count());
}

template <typename Measurement>
class MeasurementMemo {
public:
    // Returns the stored measurement if it is younger than
    // kMemoLifetimeNanos, otherwise runs `measure` and stores the result.
    template <typename Fn>
    [[nodiscard]] Measurement const& get_or_measure(Fn&& measure) noexcept {
        const std::uint64_t now = monotonic_nanos();
        const bool is_fresh = has_value_ && now >= taken_at_nanos_ && (now - taken_at_nanos_) < kMemoLifetimeNanos;
        if (!is_fresh) {
            stored_ = std::forward<Fn>(measure)();
            taken_at_nanos_ = now;
            has_value_ = true;
        }
        return stored_;
    }

    void forget() noexcept { has_value_ = false; }
    [[nodiscard]] bool has_value() const noexcept { return has_value_; }

private:
    Measurement stored_{};
    std::uint64_t taken_at_nanos_ = 0;
    bool has_value_ = false;
};

// ── Helper-thread placement ───────────────────────────────────────────

// Pins the calling thread to one CPU. Used by the helper threads of the
// probe that splits work across cores; the measuring thread itself is
// pinned by the bench harness.
//
// Returns false when the CPU is out of the process's allowed set, which
// a probe treats as "cannot measure this" rather than measuring on
// whatever core the scheduler feels like and reporting the number as if
// the placement had been honoured.
[[nodiscard]] inline bool pin_this_thread_to(int cpu) noexcept {
    if (cpu < 0) {
        return false;
    }
    cpu_set_t set{};
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned>(cpu), &set);
    // The capability proof is the same one ProbeRegion::create carries:
    // the only callers are probes reached from run_refresh under a
    // CtxFitsLedgerStore context.
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;  // SYSCALL-CAP-OK: see the proof above
}

namespace probe_support_detail::self_test {

// A tie is a tie whichever way it is written.
static_assert(gain_percent(100.0, 100.0) == 100u);
static_assert(gain_percent(200.0, 100.0) == 200u, "half the time is twice the speed");
static_assert(gain_percent(100.0, 200.0) == 50u);
// An unusable input answers zero, which reads as a refusal rather than
// as a tie. A tie and a failed measurement must not share a spelling.
static_assert(gain_percent(0.0, 100.0) == 0u);
static_assert(gain_percent(100.0, 0.0) == 0u);
static_assert(gain_percent(-1.0, 100.0) == 0u);

// The conjunction, both ways round. Statistically distinguishable but
// narrow is a tie; wide but indistinguishable is a tie.
inline constexpr VariantComparison s_significant_but_narrow{
    .baseline_p50_ns = 100.0,
    .candidate_p50_ns = 99.0,
    .candidate_gain_percent = 101u,
    .is_statistically_distinguishable = true,
    .is_practically_wide = false,
};
static_assert(s_significant_but_narrow.is_a_tie());
static_assert(!s_significant_but_narrow.candidate_wins());

inline constexpr VariantComparison s_wide_but_unsupported{
    .baseline_p50_ns = 100.0,
    .candidate_p50_ns = 50.0,
    .candidate_gain_percent = 200u,
    .is_statistically_distinguishable = false,
    .is_practically_wide = true,
};
static_assert(s_wide_but_unsupported.is_a_tie());
static_assert(!s_wide_but_unsupported.candidate_wins());

inline constexpr VariantComparison s_real_win{
    .baseline_p50_ns = 100.0,
    .candidate_p50_ns = 50.0,
    .candidate_gain_percent = 200u,
    .is_statistically_distinguishable = true,
    .is_practically_wide = true,
};
static_assert(!s_real_win.is_a_tie());
static_assert(s_real_win.candidate_wins());

// A candidate that is decisively SLOWER is not a tie and is not a win.
inline constexpr VariantComparison s_real_loss{
    .baseline_p50_ns = 100.0,
    .candidate_p50_ns = 200.0,
    .candidate_gain_percent = 50u,
    .is_statistically_distinguishable = true,
    .is_practically_wide = true,
};
static_assert(!s_real_loss.is_a_tie());
static_assert(!s_real_loss.candidate_wins());

static_assert(ProbeSettings{}.helper_count() == 0u);
static_assert(ProbeSettings{.helper_cores = {90, 91, -1, -1}}.helper_count() == 2u);
// A negative entry ends the list, so a gap does not smuggle a later
// core into the count.
static_assert(ProbeSettings{.helper_cores = {90, -1, 92, 93}}.helper_count() == 1u);

static_assert(!std::is_copy_constructible_v<ProbeRegion>);
static_assert(std::is_nothrow_move_constructible_v<ProbeRegion>);

}  // namespace probe_support_detail::self_test

}  // namespace crucible::ledger
