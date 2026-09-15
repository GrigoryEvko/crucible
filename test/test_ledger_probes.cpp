// The probe layer of the hardware-capability ledger: the contract the
// three probes share, the evidence a ratio verdict carries, and the
// policy of the background refresh.
//
// What is settled at compile time is settled there — every header in
// ledger/ and ledger/probes/ carries a self-test block, and including
// them here is what puts those blocks in a build that runs. What is left
// for this file is the behaviour a static_assert cannot reach: whether
// the kernel honours a page policy, whether the memo really shares one
// measurement, whether an unfit host really goes unprobed, and whether a
// ratio that does not reproduce is really refused.
//
// Each group states the claim and then breaks it. A test that only ever
// sees the passing case cannot tell a working gate from an absent one.

#include <crucible/ledger/RefreshDaemon.h>
#include <crucible/ledger/probes/CacheTier.h>
#include <crucible/ledger/probes/HugePage.h>
#include <crucible/ledger/probes/VectorWidth.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

using namespace crucible;
using crucible::ledger::CompetenceReport;
using crucible::ledger::CycleResult;
using crucible::ledger::EvidenceFault;
using crucible::ledger::LedgerError;
using crucible::ledger::VerdictEvidence;
using crucible::ledger::VerdictId;

namespace {

// ── Fixtures ──────────────────────────────────────────────────────────

[[nodiscard]] CompetenceReport fit_host() noexcept {
    CompetenceReport report{};
    report.isolated_core_count = 8;
    report.online_sibling_count = 0;
    report.load_average_milli = 1000;
    report.allowed_cpu_count = 384;
    report.machine_cpu_count = 384;
    report.perf_event_paranoid = 2;
    report.scaling_min_freq_khz = 4510205;
    report.scaling_max_freq_khz = 4510205;
    report.governor_is_performance = false;
    report.defects = ledger::derive_defects(report, true);
    return report;
}

[[nodiscard]] CompetenceReport unfit_host() noexcept {
    CompetenceReport report = fit_host();
    report.online_sibling_count = 4;  // an isolated core shares its pipeline
    report.defects = ledger::derive_defects(report, true);
    return report;
}

// A bench report with a chosen median and a sample set wide enough for
// Mann-Whitney to have something to rank. The samples are laid out around
// the median with a fixed spread so two fixtures with different medians
// are distinguishable and two with the same median are not.
[[nodiscard]] bench::Report synthetic_report(const char* name, double median_ns, double spread_fraction,
                                             std::size_t count = 64) {
    bench::Report report{};
    report.name = name;
    report.samples.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const double offset = (static_cast<double>(index) / static_cast<double>(count) - 0.5) * 2.0;
        report.samples[index] = median_ns * (1.0 + offset * spread_fraction);
    }
    report.pct = bench::Percentiles::compute(report.samples);
    return report;
}

// ── The shared contract ───────────────────────────────────────────────

void test_gain_percent_states_its_direction() {
    using ledger::gain_percent;
    assert(gain_percent(100.0, 100.0) == 100u);
    // Half the time is twice the speed, and the verdict says 200.
    assert(gain_percent(200.0, 100.0) == 200u);
    assert(gain_percent(100.0, 200.0) == 50u);

    // A failed measurement and a tie must not share a spelling. Zero is
    // the failure, and every probe turns it into a refusal.
    assert(gain_percent(0.0, 100.0) == 0u);
    assert(gain_percent(100.0, 0.0) == 0u);

    // Break it: a ratio large enough to overflow the percent saturates
    // rather than wrapping to something small and plausible.
    assert(gain_percent(1e300, 1.0) == 4294967295u);

    std::printf("  test_gain_percent_states_its_direction:    PASSED\n");
}

void test_ab_rule_needs_both_tests() {
    using ledger::compare_variants;

    // Wide and supported: a real win.
    const bench::Report slow = synthetic_report("slow", 200.0, 0.01);
    const bench::Report fast = synthetic_report("fast", 100.0, 0.01);
    const ledger::VariantComparison win = compare_variants(slow, fast);
    assert(win.candidate_wins());
    assert(!win.is_a_tie());
    assert(win.candidate_gain_percent == 200u);

    // Break the practical half: a one-percent difference measured over
    // sixty-four tight samples is statistically distinguishable and is
    // still not a difference anyone can act on.
    const bench::Report barely = synthetic_report("barely", 99.0, 0.0005);
    const bench::Report base = synthetic_report("base", 100.0, 0.0005);
    const ledger::VariantComparison narrow = compare_variants(base, barely);
    assert(narrow.is_statistically_distinguishable);
    assert(!narrow.is_practically_wide);
    assert(narrow.is_a_tie());
    assert(!narrow.candidate_wins());

    // Break the statistical half: two fixtures with the same median are
    // not distinguishable however the ratio comes out.
    const ledger::VariantComparison same = compare_variants(base, synthetic_report("same", 100.0, 0.0005));
    assert(same.is_a_tie());

    std::printf("  test_ab_rule_needs_both_tests:             PASSED\n");
}

void test_ratio_evidence_catches_an_unreproducible_ratio() {
    using ledger::audit_evidence;
    using ledger::evidence_for_ratio;

    // The claim: a ratio's evidence describes the ratio, not one side.
    //
    // Both sides are individually rock steady in both pairs — a within-run
    // spread of a twentieth of a percent — and the ratio still moves from
    // 2.0 to 1.0 between the pairs. The old construction stored the
    // candidate's own evidence and certified this as high confidence. The
    // ratio construction refuses it.
    const bench::Report first_baseline = synthetic_report("b1", 200.0, 0.0005);
    const bench::Report first_candidate = synthetic_report("c1", 100.0, 0.0005);
    const bench::Report second_baseline = synthetic_report("b2", 100.0, 0.0005);
    const bench::Report second_candidate = synthetic_report("c2", 100.0, 0.0005);

    const VerdictEvidence unstable =
        evidence_for_ratio(first_baseline, first_candidate, second_baseline, second_candidate);
    assert(unstable.within_run_cv_ppm < ledger::kMaxWithinRunCvPpm);
    assert(unstable.run_to_run_spread_ppm > ledger::kMaxRunToRunSpreadPpm);
    assert(audit_evidence(unstable) == EvidenceFault::RunToRunSpreadTooHigh);
    assert(ledger::derive_confidence(unstable, fit_host()) == ledger::Confidence::Unknown);

    // Unbreak it: the same four runs with the second pair reproducing the
    // first pair's ratio clear the bar.
    const bench::Report reproduced = synthetic_report("c2b", 100.0, 0.0005);
    const VerdictEvidence stable =
        evidence_for_ratio(first_baseline, first_candidate, synthetic_report("b2b", 200.0, 0.0005), reproduced);
    assert(stable.run_to_run_spread_ppm < ledger::kMaxRunToRunSpreadPpm);
    assert(audit_evidence(stable) == EvidenceFault::None);
    assert(ledger::derive_confidence(stable, fit_host()) == ledger::Confidence::High);

    // And the other half of the record still works: a noisy side pushes
    // the within-run field over its own bar even when the ratio holds.
    const VerdictEvidence noisy = evidence_for_ratio(synthetic_report("b3", 200.0, 0.40),
                                                     synthetic_report("c3", 100.0, 0.0005),
                                                     synthetic_report("b4", 200.0, 0.40),
                                                     synthetic_report("c4", 100.0, 0.0005));
    assert(audit_evidence(noisy) == EvidenceFault::WithinRunCvTooHigh);

    std::printf("  test_ratio_evidence_catches_an_unreproducible_ratio: PASSED\n");
}

void test_scratch_region_is_aligned_and_faultable() {
    auto region = ledger::ProbeRegion::create(4u * 1024u * 1024u, ledger::PagePolicy::BasePages);
    assert(region.has_value());
    assert(region->is_mapped());
    assert(region->size() == 4u * 1024u * 1024u);

    // Two-mebibyte alignment is what lets a huge-page policy take at all.
    const auto address = std::bit_cast<std::uintptr_t>(region->data());
    assert((address % ledger::ProbeRegion::kHugePageBytes) == 0u);

    assert(region->fault_in(7u) == region->size());
    const auto* bytes = static_cast<const unsigned char*>(region->data());
    assert(bytes[0] == 7u);
    assert(bytes[ledger::ProbeRegion::kBasePageBytes] == 7u);

    // Break it: a zero-length region is a caller error and not a
    // zero-length mapping.
    auto empty = ledger::ProbeRegion::create(0u, ledger::PagePolicy::BasePages);
    assert(!empty.has_value());
    assert(empty.error() == LedgerError::MalformedRecord);

    // A default-constructed region owns nothing, so a probe that failed to
    // allocate cannot be mistaken for one that succeeded.
    const ledger::ProbeRegion unmapped{};
    assert(!unmapped.is_mapped());
    assert(unmapped.size() == 0u);

    std::printf("  test_scratch_region_is_aligned_and_faultable: PASSED\n");
}

void test_page_policy_is_verified_against_the_kernel() {
    // The claim: a probe finds out what the kernel actually did rather
    // than trusting that madvise recording the advice means the fault path
    // honoured it.
    const std::size_t bytes = 8u * 1024u * 1024u;

    auto base = ledger::ProbeRegion::create(bytes, ledger::PagePolicy::BasePages);
    assert(base.has_value());
    (void)base->fault_in(1u);
    assert(ledger::probes::huge_page_detail::verify_page_policy_took(*base, ledger::PagePolicy::BasePages));
    // Break it: the same base-page region must NOT pass the huge-page
    // check, or the verifier is answering true to everything.
    assert(!ledger::probes::huge_page_detail::verify_page_policy_took(*base, ledger::PagePolicy::HugePages));

    if (concurrent::Topology::instance().hugepage_2mb_available()) {
        auto huge = ledger::ProbeRegion::create(bytes, ledger::PagePolicy::HugePages);
        assert(huge.has_value());
        (void)huge->fault_in(1u);
        // Not asserted as true: a host whose memory is fragmented enough
        // can refuse the advice, and that refusal is precisely what the
        // probe checks for before reporting a hugepage number. What IS
        // asserted is that the two policies cannot both be reported for
        // one region.
        const bool took_huge = ledger::probes::huge_page_detail::verify_page_policy_took(
            *huge, ledger::PagePolicy::HugePages);
        const bool took_base =
            ledger::probes::huge_page_detail::verify_page_policy_took(*huge, ledger::PagePolicy::BasePages);
        assert(!(took_huge && took_base));
        std::printf("    (hugepage advice %s on this host)\n", took_huge ? "took" : "was refused");
    }

    std::printf("  test_page_policy_is_verified_against_the_kernel: PASSED\n");
}

int g_memo_call_count = 0;

void test_memo_shares_one_measurement() {
    ledger::MeasurementMemo<int> memo{};
    assert(!memo.has_value());

    g_memo_call_count = 0;
    auto measure = [] {
        ++g_memo_call_count;
        return g_memo_call_count;
    };

    assert(memo.get_or_measure(measure) == 1);
    assert(memo.has_value());
    // The claim: siblings share one measurement rather than each taking
    // their own. Three reads, one measurement.
    assert(memo.get_or_measure(measure) == 1);
    assert(memo.get_or_measure(measure) == 1);
    assert(g_memo_call_count == 1);

    // Break it: forgetting the memo makes the next read measure again.
    memo.forget();
    assert(!memo.has_value());
    assert(memo.get_or_measure(measure) == 2);
    assert(g_memo_call_count == 2);

    std::printf("  test_memo_shares_one_measurement:          PASSED\n");
}

// ── The refresh policy ────────────────────────────────────────────────

int g_stub_probe_calls = 0;

[[nodiscard]] std::expected<ledger::VerdictMeasurement, LedgerError> stub_probe(CompetenceReport const&) noexcept {
    ++g_stub_probe_calls;
    VerdictEvidence evidence{};
    evidence.quantiles = cog::LatencyQuantiles{20u, 24u, 40u};
    evidence.sample_count = 100000;
    evidence.within_run_cv_ppm = 12000;
    evidence.run_to_run_spread_ppm = 4000;
    return ledger::VerdictMeasurement{.value = ledger::VerdictValue{20u}, .evidence = evidence};
}

constexpr ledger::ProbeRegistration kStubRegistry[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &stub_probe},
};
constexpr VerdictId kStubWanted[] = {VerdictId::TimerFloorNanos};

void test_an_unfit_host_is_never_probed() {
    // The claim: an unfit host is a reason to skip, not a reason to
    // measure and grade low. Measuring costs the host load it cannot
    // afford, in exchange for a number the default reader refuses anyway.
    ledger::Ledger ledger{};
    g_stub_probe_calls = 0;
    const ledger::CycleReport skipped =
        ledger::refresh_when_fit(ledger, kStubWanted, kStubRegistry, unfit_host(), 10000u);
    assert(skipped.result == CycleResult::SkippedUnfitHost);
    assert(skipped.queued_count == 1u);
    assert(g_stub_probe_calls == 0);
    assert(ledger.entries.empty());

    // Unbreak it: the very same call on a fit host does run the probe and
    // does admit the result, so the gate is the competence and not an
    // unconditional refusal.
    g_stub_probe_calls = 0;
    const ledger::CycleReport ran = ledger::refresh_when_fit(ledger, kStubWanted, kStubRegistry, fit_host(), 10000u);
    assert(ran.result == CycleResult::Admitted);
    assert(g_stub_probe_calls == 1);
    assert(ledger.entries.size() == 1u);
    assert(ledger.entries.front().confidence == ledger::Confidence::High);

    // And a second cycle on the same fresh ledger has nothing to do, so a
    // served verdict is not re-measured every period.
    g_stub_probe_calls = 0;
    const ledger::CycleReport idle = ledger::refresh_when_fit(ledger, kStubWanted, kStubRegistry, fit_host(), 10001u);
    assert(idle.result == CycleResult::NothingToDo);
    assert(g_stub_probe_calls == 0);

    std::printf("  test_an_unfit_host_is_never_probed:        PASSED\n");
}

// A measurement shared by three sibling verdicts, in the shape the real
// probes use: one memo, three probe functions that all read it.
ledger::MeasurementMemo<int> g_sibling_memo{};
int g_sibling_measure_count = 0;

[[nodiscard]] int shared_sibling_measurement() noexcept {
    return g_sibling_memo.get_or_measure([] { return ++g_sibling_measure_count; });
}

[[nodiscard]] std::expected<ledger::VerdictMeasurement, LedgerError> sibling_probe(CompetenceReport const&) noexcept {
    const int shared = shared_sibling_measurement();
    VerdictEvidence evidence{};
    evidence.quantiles = cog::LatencyQuantiles{20u, 24u, 40u};
    evidence.sample_count = 100000;
    evidence.within_run_cv_ppm = 12000;
    evidence.run_to_run_spread_ppm = 4000;
    return ledger::VerdictMeasurement{.value = ledger::VerdictValue{static_cast<std::uint64_t>(shared)},
                                      .evidence = evidence};
}

constexpr ledger::ProbeRegistration kSiblingRegistry[] = {
    {.id = VerdictId::TimerFloorNanos, .run = &sibling_probe},
    {.id = VerdictId::VectorWidthPreferredBits, .run = &sibling_probe},
    {.id = VerdictId::VectorWidthComputeGainPercent, .run = &sibling_probe},
};
constexpr VerdictId kSiblingWanted[] = {
    VerdictId::TimerFloorNanos,
    VerdictId::VectorWidthPreferredBits,
    VerdictId::VectorWidthComputeGainPercent,
};

void test_sibling_verdicts_share_one_measurement() {
    // The claim, and the regression it pins.
    //
    // Two of the three real probes answer several verdicts from one
    // measurement. The memo is what shares it, and the memo has a
    // lifetime. That lifetime was five seconds first, and the hugepage
    // measurement takes about twenty, so each sibling expired the memo and
    // measured again — under the load the previous measurement had just
    // created. The two hugepage fault verdicts came out at 40.3 and 43.7
    // microseconds per mebibyte in one run of crucible-hwprobe, describing
    // a machine that had not changed.
    //
    // What must hold is that one refresh cycle takes one measurement,
    // whatever the siblings are. That is asserted by the count, and the
    // values agreeing is the same fact seen from the other side.
    ledger::Ledger ledger{};
    g_sibling_memo.forget();
    g_sibling_measure_count = 0;

    const ledger::CycleReport report =
        ledger::refresh_when_fit(ledger, kSiblingWanted, kSiblingRegistry, fit_host(), 10000u);
    assert(report.result == CycleResult::Admitted);
    assert(report.outcome.measured_count == 3u);
    assert(report.outcome.admitted_count == 3u);
    assert(g_sibling_measure_count == 1);

    assert(ledger.entries.size() == 3u);
    for (ledger::LedgerEntry const& entry : ledger.entries) {
        assert(entry.value.value().raw() == 1u);
    }

    // Break it: a memo that expires between siblings measures once per
    // sibling and the three verdicts stop agreeing. Forcing the expiry by
    // hand is what the five-second lifetime did by itself.
    ledger::Ledger second{};
    g_sibling_memo.forget();
    g_sibling_measure_count = 0;
    for (const VerdictId id : kSiblingWanted) {
        g_sibling_memo.forget();  // stands in for the lifetime running out
        const VerdictId one[] = {id};
        (void)ledger::refresh_when_fit(second, one, kSiblingRegistry, fit_host(), 10000u);
    }
    assert(g_sibling_measure_count == 3);
    assert(second.entries.size() == 3u);
    assert(second.entries[0].value.value().raw() != second.entries[2].value.value().raw());

    // And the bound that keeps the fix from overshooting: a memo may not
    // outlive the shortest time to live in the ledger, or a cycle could
    // serve a verdict the ledger has already called stale.
    static_assert(ledger::kMemoLifetimeSeconds < ledger::kFragmentationTtlSeconds);

    std::printf("  test_sibling_verdicts_share_one_measurement: PASSED\n");
}

void test_backoff_grows_only_when_nothing_was_admitted() {
    const ledger::RefreshSchedule schedule{
        .idle_period_seconds = 3600,
        .initial_backoff_seconds = 60,
        .max_backoff_seconds = 480,
        .backoff_multiplier = 2,
    };
    assert(schedule.is_well_formed());
    assert(schedule.next_backoff(0) == 60u);
    assert(schedule.next_backoff(60) == 120u);
    assert(schedule.next_backoff(480) == 480u);

    // Break it: a schedule that does not back off is not well formed, and
    // the mint's precondition is what refuses it.
    assert(!ledger::RefreshSchedule{.backoff_multiplier = 1}.is_well_formed());

    // Only an outcome that got somewhere resets the wait. A skipped cycle
    // on an unfit host must back off, or an unfit host is looked at every
    // idle period forever.
    assert(ledger::outcome_resets_backoff(CycleResult::Admitted));
    assert(ledger::outcome_resets_backoff(CycleResult::NothingToDo));
    assert(!ledger::outcome_resets_backoff(CycleResult::SkippedUnfitHost));
    assert(!ledger::outcome_resets_backoff(CycleResult::AllRefused));
    assert(!ledger::outcome_resets_backoff(CycleResult::CommitFailed));

    std::printf("  test_backoff_grows_only_when_nothing_was_admitted: PASSED\n");
}

void test_daemon_publishes_a_view_a_reader_can_use() {
    constexpr ledger::LedgerIoCtx ctx{};
    ledger::RefreshDaemonConfig config{};
    config.wanted = kStubWanted;
    config.registry = kStubRegistry;
    config.probe_settings = ledger::ProbeSettings{.sample_count = 64, .pin_core = -1};

    auto daemon = ledger::mint_refresh_daemon(ctx, config);
    assert(daemon != nullptr);

    // A reader before any cycle gets a view, not a null pointer. Every
    // lookup on it misses, so every consumer takes its conservative path.
    const std::shared_ptr<const ledger::LedgerView> before = daemon->current_view();
    assert(before != nullptr);
    const auto miss = before->lookup(VerdictId::ParallelKneeBytes);
    assert(!miss.is_known());
    assert(miss.value_or_conservative(ledger::VerdictValue{4242u}) == ledger::VerdictValue{4242u});

    // One cycle on the calling thread, which is the same code the loop
    // runs, so the test exercises the real thing.
    g_stub_probe_calls = 0;
    const ledger::CycleReport report = daemon->run_one_cycle();
    assert(daemon->completed_cycle_count() == 1u);
    assert(daemon->last_result() == report.result);

    // What the outcome is depends on whether the machine running the test
    // is fit to measure, and both answers are correct. What must hold
    // either way is that a published view is always readable and that a
    // skipped cycle ran no probe.
    if (report.result == CycleResult::SkippedUnfitHost) {
        assert(g_stub_probe_calls == 0);
    } else {
        assert(report.result == CycleResult::Admitted || report.result == CycleResult::CommitFailed
               || report.result == CycleResult::NothingToDo);
    }
    const std::shared_ptr<const ledger::LedgerView> after = daemon->current_view();
    assert(after != nullptr);

    std::printf("  test_daemon_publishes_a_view_a_reader_can_use: PASSED (%.*s)\n",
                static_cast<int>(ledger::cycle_result_name(report.result).size()),
                ledger::cycle_result_name(report.result).data());
}

void test_daemon_thread_starts_and_stops() {
    constexpr ledger::LedgerIoCtx ctx{};
    ledger::RefreshDaemonConfig config{};
    config.wanted = kStubWanted;
    config.registry = kStubRegistry;
    // A long idle period on purpose: stop() must not wait it out, and a
    // test that passed only because the period was short would prove
    // nothing about the wake-up.
    config.schedule.idle_period_seconds = 6u * 3600u;
    config.probe_settings = ledger::ProbeSettings{.sample_count = 64, .pin_core = -1};

    auto daemon = ledger::mint_refresh_daemon(ctx, config);
    daemon->start();
    // The first cycle happens before the first sleep, so waiting for one
    // completed cycle needs no timer.
    while (daemon->completed_cycle_count() == 0u) {
        CRUCIBLE_SPIN_PAUSE;
    }
    daemon->stop();
    assert(daemon->completed_cycle_count() >= 1u);
    // Idempotent: stopping twice, and destroying after a stop, must not
    // join a thread that is already gone.
    daemon->stop();

    std::printf("  test_daemon_thread_starts_and_stops:       PASSED\n");
}

// ── The probes themselves ─────────────────────────────────────────────

void test_vector_width_probe_answers_or_declines() {
    // Cheap settings: the probe is exercised for its structure — the two
    // shapes, the memo, the guard that keeps a 512-bit kernel off a host
    // without one — and not for a number worth storing. The numbers this
    // host actually produces come from crucible-hwprobe, which measures at
    // the sample counts the verdicts want.
    ledger::set_probe_settings(ledger::ProbeSettings{.sample_count = 64, .pin_core = -1});
    ledger::probes::vector_width_detail::g_memo.forget();

    const auto preferred = ledger::probes::probe_vector_width_preferred_bits(fit_host());

    if constexpr (ledger::kBuildIsInstrumented) {
        // The claim: an instrumented build declines rather than reporting
        // numbers about its own instrumentation. This is not a tolerated
        // skip, it is the assertion — the same probe under
        // AddressSanitizer once reported the streaming shape at 190% where
        // an ordinary build reports 101%, because a sanitizer charges one
        // shadow check per load and a 512-bit load moves twice the bytes
        // of a 256-bit one.
        assert(!preferred.has_value());
        assert(preferred.error() == LedgerError::NotApplicableOnThisHost);
        assert(!ledger::probes::probe_vector_width_compute_gain(fit_host()).has_value());
        assert(!ledger::probes::probe_vector_width_memory_gain(fit_host()).has_value());
        std::printf("  test_vector_width_probe_answers_or_declines: PASSED (instrumented build declines)\n");
        return;
    }

    if (!preferred.has_value()) {
        // The only declines an uninstrumented host produces: no wide unit
        // to compare against, or no memory for the streaming buffer.
        assert(preferred.error() == LedgerError::NotApplicableOnThisHost
               || preferred.error() == LedgerError::StorePathUnavailable);
        std::printf("  test_vector_width_probe_answers_or_declines: PASSED (declined)\n");
        return;
    }

    const std::uint64_t width = preferred->value.raw();
    assert(width == ledger::probes::kNarrowWidthBits || width == ledger::probes::kWideWidthBits);

    // The two margins come from the SAME measurement, because the memo is
    // what shares it. If they did not, each verdict would describe a
    // different moment of a machine that had not changed.
    const auto compute = ledger::probes::probe_vector_width_compute_gain(fit_host());
    const auto memory = ledger::probes::probe_vector_width_memory_gain(fit_host());
    assert(compute.has_value());
    assert(memory.has_value());

    // The rule the probe applies, restated: the wide width is preferred
    // only when it won the compute shape outright.
    if (width == ledger::probes::kWideWidthBits) {
        assert(compute->value.raw() > 100u + ledger::kPracticalMarginPercent);
    }
    std::printf("  test_vector_width_probe_answers_or_declines: PASSED (%llu bits, compute=%llu%%, memory=%llu%%)\n",
                static_cast<unsigned long long>(width), static_cast<unsigned long long>(compute->value.raw()),
                static_cast<unsigned long long>(memory->value.raw()));
}

void test_every_verdict_id_has_a_name_and_a_trait() {
    // A verdict whose trait has no name can never be read back, because
    // verdict_id_from_name is the only way in from a stored file. The
    // enum and the trait table are edited separately, so this is the one
    // check that catches an id added to one and not the other.
    for (std::uint16_t raw = 0; raw < ledger::kVerdictIdCount; ++raw) {
        const auto id = static_cast<VerdictId>(raw);
        const ledger::VerdictTrait trait = ledger::verdict_trait(id);
        assert(!trait.name.empty());
        const auto round_tripped = ledger::verdict_id_from_name(trait.name);
        assert(round_tripped.has_value());
        assert(*round_tripped == id);
        // A time to live of zero is the never-expires sentinel, which no
        // measured verdict wants: every one of these depends on something
        // the fingerprint does not fold.
        assert(!trait.ttl.never_expires());
    }

    // Break it: an id past the end of the enum has no name, so a file
    // written by a newer build cannot be misread as a question this build
    // knows.
    const auto beyond = static_cast<VerdictId>(ledger::kVerdictIdCount);
    assert(ledger::verdict_trait(beyond).name.empty());

    std::printf("  test_every_verdict_id_has_a_name_and_a_trait: PASSED (%u ids)\n", ledger::kVerdictIdCount);
}

}  // namespace

int main() {
    // The daemon commits to the store, and the store's root comes from the
    // environment. Without this the test would write a stub verdict into
    // whatever real ledger the machine is using and the next process to
    // read it would be served a fabricated twenty nanoseconds. setenv
    // rather than a parameter, so the path sanitizer the discovery runs
    // through is still exercised.
    char directory_template[] = "/tmp/crucible-probe-test-XXXXXX";
    const char* directory = ::mkdtemp(directory_template);
    assert(directory != nullptr);
    assert(::setenv("XDG_CACHE_HOME", directory, 1) == 0);

    std::printf("test_ledger_probes:\n");
    test_gain_percent_states_its_direction();
    test_ab_rule_needs_both_tests();
    test_ratio_evidence_catches_an_unreproducible_ratio();
    test_scratch_region_is_aligned_and_faultable();
    test_page_policy_is_verified_against_the_kernel();
    test_memo_shares_one_measurement();
    test_an_unfit_host_is_never_probed();
    test_sibling_verdicts_share_one_measurement();
    test_backoff_grows_only_when_nothing_was_admitted();
    test_daemon_publishes_a_view_a_reader_can_use();
    test_daemon_thread_starts_and_stops();
    test_vector_width_probe_answers_or_declines();
    test_every_verdict_id_has_a_name_and_a_trait();
    std::printf("test_ledger_probes: 13 groups, all passed\n");
    return 0;
}
