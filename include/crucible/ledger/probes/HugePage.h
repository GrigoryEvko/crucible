#pragma once

// #70 — what madvise(MADV_HUGEPAGE) costs on this host, and whether it
// pays that back.
//
// The arena and the MetaLog both ask for huge pages, so the cost of
// asking is on the startup path of every Crucible process. The premise
// worth testing is that asking is expensive: with
// /sys/kernel/mm/transparent_hugepage/defrag set to `madvise` or
// `always`, a fault on a region that asked for huge pages can drive
// synchronous compaction, and compaction on a fragmented machine can
// stall for a long time. That is a real failure mode and it is worth
// knowing whether this host is in it.
//
// It is not, on every host, the failure mode people expect. The opposite
// is also common: a 2 MiB page is one fault instead of five hundred and
// twelve, and the kernel zeroes it with wider stores, so on a machine
// with free contiguous memory asking for huge pages makes fault-in
// several times CHEAPER rather than more expensive. The probe reports a
// signed direction rather than assuming one.
//
// Two measurements, because "cheap" and "worth it" are different
// questions:
//
//   fault-in   Map a fresh region, ask for one page policy, write one
//              byte to every base page, and time that. A fresh mapping
//              every sample, because the second touch of the same region
//              is free and would measure nothing. The cost is reported
//              per mebibyte so it composes: an arena four times the size
//              costs four times as much, to first order.
//
//   payback    A pointer chase over a random permutation of the lines of
//              a region far larger than the TLB can cover, with the two
//              page policies. The chase defeats the prefetcher, so every
//              step pays a translation, which is the only thing a page
//              size can change once the pages are there. If huge pages
//              pay back at all in steady state, this is the shape that
//              shows it.
//
// The cost this probe measures has the shortest time to live in the
// ledger, and the reason is in Verdict.h: the number is set by how
// fragmented physical memory happens to be, and fragmentation is a
// running average of everything the machine has allocated since boot.
// Every other verdict describes the machine. This one describes its mood.
//
// A note on the variance bar. This probe sits closest to it of the three.
// A huge-page fault either finds a free 2 MiB block or goes looking for
// one, and those two outcomes differ by an order of magnitude, so the
// within-run spread is wide by nature and not because the host is
// misbehaving. Enlarging the region shrinks the spread, because a sample
// then averages over more faults — measured on the development host, a
// 16 MiB region gave a 22% coefficient of variation and a 64 MiB region
// gave 5%, with the medians agreeing to 1%. The region below is sized for
// that, and the probe still gets refused on a run that hits a compaction
// stall. That refusal is the mechanism working, not a defect in it.
//
// Release behaviour. `verify_page_policy_took` is a runtime read of
// /proc/self/smaps and a runtime branch, not an assertion, because it is
// the only thing standing between "the kernel ignored the advice" and a
// verdict that claims to be about huge pages while describing base ones —
// and a check that compiles out under NDEBUG could not stand between
// anything.

#include <crucible/ledger/ProbeSupport.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <utility>
#include <vector>

namespace crucible::ledger::probes {

// Sized so one sample averages over thirty-two huge-page faults. Refer to
// the variance note above for why that number and not a smaller one.
inline constexpr std::size_t kFaultRegionBytes = 64ull * 1024ull * 1024ull;

// The chase region has to exceed what the TLB can cover with base pages
// by a wide margin, or the comparison is between two fully-covered
// working sets and the page size cannot matter. A large second-level TLB
// holds a few thousand entries; 256 MiB of base pages needs sixty-five
// thousand.
inline constexpr std::size_t kChaseRegionBytes = 256ull * 1024ull * 1024ull;

inline constexpr std::size_t kChaseSteps = 1ull << 20;

// Fault-in samples. Each one maps, touches and unmaps 64 MiB, so the run
// is seconds rather than milliseconds and the count is fixed rather than
// taken from the configured budget.
inline constexpr std::size_t kFaultSampleCount = 48;

namespace huge_page_detail {

// How many kibibytes of one mapping the kernel actually backed with huge
// pages. Read from smaps rather than inferred from the madvise return
// value, because madvise succeeds when it records the advice and says
// nothing about whether the fault path was able to honour it.
[[nodiscard]] inline std::size_t anon_huge_kib_for(void const* address) noexcept {
    std::FILE* maps = std::fopen("/proc/self/smaps", "re");
    if (maps == nullptr) {
        return 0u;
    }
    const auto wanted = std::bit_cast<std::uintptr_t>(address);
    char line[512];
    bool inside_target = false;
    std::size_t huge_kib = 0;
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        if (std::sscanf(line, "%llx-%llx", &start, &end) == 2) {
            inside_target = (wanted >= static_cast<std::uintptr_t>(start) && wanted < static_cast<std::uintptr_t>(end));
            continue;
        }
        if (!inside_target) {
            continue;
        }
        constexpr std::string_view kKey = "AnonHugePages:";
        if (std::string_view{line}.substr(0, kKey.size()) == kKey) {
            unsigned long kib = 0;
            if (std::sscanf(line + kKey.size(), "%lu", &kib) == 1) {
                huge_kib = static_cast<std::size_t>(kib);
            }
            break;
        }
    }
    (void)std::fclose(maps);
    return huge_kib;
}

// Did the kernel honour the advice? A region that asked for huge pages
// and got none is a region whose measurement is about base pages under
// another name.
[[nodiscard]] inline bool verify_page_policy_took(ProbeRegion const& region, PagePolicy policy) noexcept {
    const std::size_t huge_kib = anon_huge_kib_for(region.data());
    if (policy == PagePolicy::HugePages) {
        // Half is a deliberately loose bar. The kernel is free to back
        // part of a region with base pages when it cannot find contiguous
        // memory for all of it, and a region that is mostly huge pages is
        // still measuring huge-page behaviour.
        return huge_kib * 2u >= region.size() / 1024u;
    }
    return huge_kib == 0u;
}

[[nodiscard]] inline bench::Run configured_run(const char* name, std::size_t samples, std::size_t wall_ms) noexcept {
    bench::Run run{name};
    (void)run.samples(samples).warmup(2).max_wall_ms(wall_ms);
    const int core = probe_settings().pin_core;
    if (core >= 0) {
        (void)run.core(core);
    }
    return run;
}

// One fault-in sample: a fresh mapping, the advice, a write to every base
// page, then the unmap. The unmap is inside the timed body on purpose —
// it is part of what an arena pays for its pages, and excluding it would
// report a cost the caller never actually sees.
struct FaultInOutcome {
    double nanos_per_mib = 0;
    bool policy_took = false;
    VerdictEvidence evidence{};
    // Kept so the gain verdict can build ratio evidence from two
    // independent A/B pairs rather than from one side's steadiness.
    bench::Report first{};
    bench::Report second{};
};

[[nodiscard]] inline FaultInOutcome measure_fault_in(PagePolicy policy, const char* first_name,
                                                     const char* second_name) noexcept {
    FaultInOutcome outcome{};
    bool every_sample_took = true;

    auto one_run = [&](const char* name) {
        return configured_run(name, kFaultSampleCount, 30000).measure([&] {
            auto region = ProbeRegion::create(kFaultRegionBytes, policy);
            if (!region.has_value()) {
                every_sample_took = false;
                return;
            }
            (void)region->fault_in(1u);
            if (!verify_page_policy_took(*region, policy)) {
                every_sample_took = false;
            }
            bench::do_not_optimize(region->data());
        });
    };

    bench::Report first = one_run(first_name);
    bench::Report second = one_run(second_name);

    outcome.policy_took = every_sample_took;
    outcome.evidence = evidence_from_two_runs(first, second);

    constexpr double kMibPerRegion = static_cast<double>(kFaultRegionBytes) / (1024.0 * 1024.0);
    outcome.nanos_per_mib = std::min(first.pct.p50, second.pct.p50) / kMibPerRegion;

    // The stored evidence describes one whole region, and the value
    // describes one mebibyte of it. Rescaling the quantiles keeps the two
    // in the same unit, so a reader comparing the value against the p50 is
    // comparing like with like rather than finding them a factor of
    // sixty-four apart with nothing saying why.
    outcome.evidence.quantiles.p50_ns = saturating_nanos(outcome.nanos_per_mib);
    outcome.evidence.quantiles.p99_ns =
        saturating_nanos(std::max(first.pct.p99, second.pct.p99) / kMibPerRegion);
    outcome.evidence.quantiles.p999_ns =
        saturating_nanos(std::max(first.pct.p99_9, second.pct.p99_9) / kMibPerRegion);
    outcome.evidence.quantiles.p99_ns = std::max(outcome.evidence.quantiles.p99_ns, outcome.evidence.quantiles.p50_ns);
    outcome.evidence.quantiles.p999_ns =
        std::max(outcome.evidence.quantiles.p999_ns, outcome.evidence.quantiles.p99_ns);

    // Moved rather than copied: bench::Report deletes its copy assignment
    // on purpose, and everything above has already read what it needed.
    outcome.first = std::move(first);
    outcome.second = std::move(second);
    return outcome;
}

// Builds a cycle through every line of the region in random order, where
// each line's first word holds the byte offset of the next line. The
// chase then cannot be prefetched, and every step pays one translation.
inline void build_random_line_cycle(std::uint64_t* words, std::size_t line_count, std::uint64_t seed) noexcept {
    constexpr std::size_t kWordsPerLine = 8;
    std::vector<std::uint32_t> order(line_count);
    for (std::size_t index = 0; index < line_count; ++index) {
        order[index] = static_cast<std::uint32_t>(index);
    }
    // xorshift64, so the permutation is the same on every host and every
    // run. A chase built from a different permutation would be a
    // different measurement wearing the same name.
    std::uint64_t state = (seed == 0u) ? 0x9e3779b97f4a7c15ull : seed;
    for (std::size_t index = line_count - 1u; index > 0u; --index) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const std::size_t pick = state % (index + 1u);
        std::swap(order[index], order[pick]);
    }
    for (std::size_t index = 0; index < line_count; ++index) {
        const std::size_t here = static_cast<std::size_t>(order[index]) * kWordsPerLine;
        const std::size_t next = static_cast<std::size_t>(order[(index + 1u) % line_count]) * kWordsPerLine;
        words[here] = static_cast<std::uint64_t>(next);
    }
}

[[gnu::noinline]] inline std::size_t chase(const std::uint64_t* words, std::size_t steps) noexcept {
    std::size_t cursor = 0;
    for (std::size_t step = 0; step < steps; ++step) {
        cursor = static_cast<std::size_t>(words[cursor]);
    }
    return cursor;
}

}  // namespace huge_page_detail

struct HugePageMeasurement {
    LedgerError fault = LedgerError::NotApplicableOnThisHost;

    std::uint64_t fault_cost_nanos_per_mib = 0;
    // Above 100 means asking for huge pages made fault-in cheaper.
    std::uint32_t fault_gain_percent = 0;
    // Above 100 means the chase ran faster on huge pages.
    std::uint32_t access_gain_percent = 0;

    VerdictEvidence fault_evidence{};
    VerdictEvidence fault_gain_evidence{};
    VerdictEvidence access_evidence{};

    [[nodiscard]] constexpr bool is_usable() const noexcept { return fault == LedgerError::None; }
};

namespace huge_page_detail {

inline MeasurementMemo<HugePageMeasurement> g_memo{};

[[nodiscard]] inline HugePageMeasurement measure() noexcept {
    HugePageMeasurement result{};

    // An instrumented build measures its own instrumentation. Refer to
    // kBuildIsInstrumented in ProbeSupport.h for what it costs.
    if constexpr (kBuildIsInstrumented) {
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }


    if (!concurrent::Topology::instance().hugepage_2mb_available()) {
        // The kernel has transparent hugepages switched off entirely, so
        // there is no advice to give and nothing to compare. Not a
        // refusal: the host has answered.
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }

    const FaultInOutcome huge =
        measure_fault_in(PagePolicy::HugePages, "ledger.thp.fault.huge.run1", "ledger.thp.fault.huge.run2");
    if (!huge.policy_took) {
        // The advice was recorded and the fault path did not honour it, so
        // whatever was measured was not huge pages. Reporting it would put
        // a base-page number under a huge-page name, which is the one
        // outcome worse than reporting nothing.
        result.fault = LedgerError::NotApplicableOnThisHost;
        return result;
    }
    const FaultInOutcome base =
        measure_fault_in(PagePolicy::BasePages, "ledger.thp.fault.base.run1", "ledger.thp.fault.base.run2");

    result.fault_cost_nanos_per_mib = saturating_nanos(huge.nanos_per_mib);
    result.fault_gain_percent = gain_percent(base.nanos_per_mib, huge.nanos_per_mib);
    // The cost verdict is one measurement and keeps that measurement's
    // evidence. The gain verdict is a ratio and gets ratio evidence, so a
    // fault cost that reproduces and a gain that does not are refused
    // independently rather than as a pair.
    result.fault_evidence = huge.evidence;
    result.fault_gain_evidence = evidence_for_ratio(base.first, huge.first, base.second, huge.second);

    // ── Payback ──
    auto huge_region = ProbeRegion::create(kChaseRegionBytes, PagePolicy::HugePages);
    auto base_region = ProbeRegion::create(kChaseRegionBytes, PagePolicy::BasePages);
    if (!huge_region.has_value() || !base_region.has_value()) {
        result.fault = LedgerError::StorePathUnavailable;
        return result;
    }
    (void)huge_region->fault_in(1u);
    (void)base_region->fault_in(1u);

    const std::size_t line_count = kChaseRegionBytes / 64u;
    auto* huge_words = static_cast<std::uint64_t*>(huge_region->data());
    auto* base_words = static_cast<std::uint64_t*>(base_region->data());
    // The same seed for both, so the two chases walk the same permutation
    // and the only difference between them is the page size.
    build_random_line_cycle(huge_words, line_count, 0x5bd1e995ull);
    build_random_line_cycle(base_words, line_count, 0x5bd1e995ull);

    // Not volatile. bench::do_not_optimize is the barrier, and a
    // volatile accumulator would add a store to memory inside the
    // timed body that the measured code does not have.
    std::size_t sink = 0;
    auto chase_run = [&](const char* name, const std::uint64_t* words) {
        return configured_run(name, kMinSampleCount, 30000).measure([&] {
            sink += chase(words, kChaseSteps);
            bench::do_not_optimize(sink);
        });
    };
    // Both sides twice, interleaved base-huge-base-huge, so the two A/B
    // pairs are separated in time. Running base twice and then huge twice
    // would put every base sample before every huge one, and a machine
    // whose load drifted during the probe would report that drift as a
    // page-size effect.
    const bench::Report base_chase_first = chase_run("ledger.thp.chase.base.run1", base_words);
    const bench::Report huge_chase_first = chase_run("ledger.thp.chase.huge.run1", huge_words);
    const bench::Report base_chase_second = chase_run("ledger.thp.chase.base.run2", base_words);
    const bench::Report huge_chase_second = chase_run("ledger.thp.chase.huge.run2", huge_words);
    bench::do_not_optimize(sink);

    const VariantComparison payback = compare_variants(base_chase_first, huge_chase_first);
    // A gain inside the noise band is reported as exactly parity rather
    // than as the raw ratio. The raw number would be a true measurement of
    // nothing, and a caller reading 101 has no way to tell it from a real
    // one percent.
    result.access_gain_percent = payback.is_a_tie() ? 100u : payback.candidate_gain_percent;
    result.access_evidence =
        evidence_for_ratio(base_chase_first, huge_chase_first, base_chase_second, huge_chase_second);

    result.fault = LedgerError::None;
    return result;
}

[[nodiscard]] inline HugePageMeasurement const& shared_measurement() noexcept {
    return g_memo.get_or_measure(&measure);
}

}  // namespace huge_page_detail

// ── The three ProbeFunctions ──────────────────────────────────────────

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_thp_fault_cost(CompetenceReport const&) noexcept {
    HugePageMeasurement const& measured = huge_page_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.fault_cost_nanos_per_mib},
                              .evidence = measured.fault_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_thp_fault_gain(CompetenceReport const&) noexcept {
    HugePageMeasurement const& measured = huge_page_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (measured.fault_gain_percent == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.fault_gain_percent},
                              .evidence = measured.fault_gain_evidence};
}

[[nodiscard]] inline std::expected<VerdictMeasurement, LedgerError>
probe_thp_access_gain(CompetenceReport const&) noexcept {
    HugePageMeasurement const& measured = huge_page_detail::shared_measurement();
    if (!measured.is_usable()) {
        return std::unexpected(measured.fault);
    }
    if (measured.access_gain_percent == 0u) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    return VerdictMeasurement{.value = VerdictValue{measured.access_gain_percent},
                              .evidence = measured.access_evidence};
}

namespace huge_page_detail::self_test {

static_assert(!HugePageMeasurement{}.is_usable());
static_assert(HugePageMeasurement{}.fault == LedgerError::NotApplicableOnThisHost);

// The chase region must be far past what a base-page TLB covers, or the
// payback measurement cannot see a page size at all.
static_assert(kChaseRegionBytes / 4096u > 60000u, "the chase must outrun any plausible base-page TLB");
// The fault region must be a whole number of huge pages, or a sample
// averages over a fractional one.
static_assert(kFaultRegionBytes % (2ull * 1024ull * 1024ull) == 0u);
static_assert(kFaultSampleCount >= kMinSampleCount);

}  // namespace huge_page_detail::self_test

}  // namespace crucible::ledger::probes
