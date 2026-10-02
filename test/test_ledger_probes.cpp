// The probe layer of the hardware-capability ledger, the part that no probe
// family owns: the contract that the probes share, the evidence that a ratio
// verdict carries, and the names of the verdicts.
//
// The refresh policy and each probe family have a test of their own:
// test_ledger_probes_refresh, test_ledger_probes_cache_tier,
// test_ledger_probes_huge_page and test_ledger_probes_vector_width.  This
// file tests the behavior that a static_assert cannot reach: whether the
// memo shares one measurement, and whether a ratio that does not reproduce
// is refused.
//
// Each group states the claim and then breaks it. A test that only ever
// sees the passing case cannot tell a working gate from an absent one.

#include <crucible/ledger/ProbeSupport.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace crucible;
using crucible::ledger::EvidenceFault;
using crucible::ledger::LedgerError;
using crucible::ledger::VerdictEvidence;
using crucible::ledger::VerdictId;
using ledger_probe_fixtures::fit_host;
using ledger_probe_fixtures::probe_ctx;

namespace {

// ── Fixtures ──────────────────────────────────────────────────────────

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

    crucible::test::pass("  test_gain_percent_states_its_direction:    PASSED\n");
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

    crucible::test::pass("  test_ab_rule_needs_both_tests:             PASSED\n");
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
    const VerdictEvidence noisy =
        evidence_for_ratio(synthetic_report("b3", 200.0, 0.40), synthetic_report("c3", 100.0, 0.0005),
                           synthetic_report("b4", 200.0, 0.40), synthetic_report("c4", 100.0, 0.0005));
    assert(audit_evidence(noisy) == EvidenceFault::WithinRunCvTooHigh);

    crucible::test::pass("  test_ratio_evidence_catches_an_unreproducible_ratio: PASSED\n");
}

void test_scratch_region_is_aligned_and_faultable() {
    auto region = ledger::ProbeRegion::create(probe_ctx, 4u * 1024u * 1024u, ledger::PagePolicy::BasePages);
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
    auto empty = ledger::ProbeRegion::create(probe_ctx, 0u, ledger::PagePolicy::BasePages);
    assert(!empty.has_value());
    assert(empty.error() == LedgerError::MalformedRecord);

    // A default-constructed region owns nothing, so a probe that failed to
    // allocate cannot be mistaken for one that succeeded.
    const ledger::ProbeRegion unmapped{};
    assert(!unmapped.is_mapped());
    assert(unmapped.size() == 0u);

    crucible::test::pass("  test_scratch_region_is_aligned_and_faultable: PASSED\n");
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

    crucible::test::pass("  test_memo_shares_one_measurement:          PASSED\n");
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

    crucible::test::pass("  test_every_verdict_id_has_a_name_and_a_trait: PASSED ({} ids)\n", ledger::kVerdictIdCount);
}

}  // namespace

namespace crucible::ledger {

static_assert(kMemoLifetimeSeconds < kFragmentationTtlSeconds,
              "a memo that outlives the shortest TTL could serve a verdict the ledger calls stale");

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

static_assert(!std::is_copy_constructible_v<ProbeRegion>);
static_assert(std::is_nothrow_move_constructible_v<ProbeRegion>);

}  // namespace probe_support_detail::self_test

}  // namespace crucible::ledger

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_ledger_probes:\n");
    test_gain_percent_states_its_direction();
    test_ab_rule_needs_both_tests();
    test_ratio_evidence_catches_an_unreproducible_ratio();
    test_scratch_region_is_aligned_and_faultable();
    test_memo_shares_one_measurement();
    test_every_verdict_id_has_a_name_and_a_trait();
    crucible::test::pass("test_ledger_probes: 6 groups, all passed\n");
    return 0;
}
