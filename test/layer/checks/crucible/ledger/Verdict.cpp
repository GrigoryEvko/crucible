// The compile-time checks of crucible/ledger/Verdict.h.

#include <crucible/ledger/Verdict.h>

namespace crucible::ledger {

static_assert(sizeof(VerdictValue) == sizeof(std::uint64_t));

static_assert(std::is_trivially_copyable_v<VerdictEvidence>);

static_assert(std::is_trivially_copyable_v<LedgerEntry>);

static_assert(std::is_trivially_copyable_v<VerdictLookup>);

namespace verdict_detail::self_test {

inline constexpr CompetenceReport s_fit{
    .defects = {},
    .isolated_core_count = 8,
    .online_sibling_count = 0,
    .load_average_milli = 1000,
    .allowed_cpu_count = 384,
    .perf_event_paranoid = 2,
    .scaling_min_freq_khz = 4510205,
    .scaling_max_freq_khz = 4510205,
    .governor_is_performance = false,
};
static_assert(s_fit.is_competent());

inline constexpr CompetenceReport s_unfit = [] {
    CompetenceReport report = s_fit;
    report.online_sibling_count = 4;
    report.defects = derive_defects(report, true);
    return report;
}();
static_assert(!s_unfit.is_competent());

inline constexpr VerdictEvidence s_good{
    .quantiles = cog::LatencyQuantiles{20u, 24u, 40u},
    .sample_count = 100000,
    .within_run_cv_ppm = 12000,
    .run_to_run_spread_ppm = 4000,
};
static_assert(is_evidence_sound(s_good));
static_assert(derive_confidence(s_good, s_fit) == Confidence::High);
static_assert(derive_confidence(s_good, s_unfit) == Confidence::Low);

// The exact shape the bench harness produced when it compared nanoseconds
// against a millisecond budget: 128 samples where 100 000 were asked for.
inline constexpr VerdictEvidence s_starved = [] {
    VerdictEvidence evidence = s_good;
    evidence.sample_count = 128;
    return evidence;
}();
static_assert(is_evidence_sound(s_starved), "128 samples clears the 32-sample floor");
inline constexpr VerdictEvidence s_too_few = [] {
    VerdictEvidence evidence = s_good;
    evidence.sample_count = 31;
    return evidence;
}();
static_assert(!is_evidence_sound(s_too_few));
static_assert(audit_evidence(s_too_few) == EvidenceFault::TooFewSamples);
static_assert(derive_confidence(s_too_few, s_fit) == Confidence::Unknown);

// A throttling part: 6% within-run cv is over the house bar.
inline constexpr VerdictEvidence s_noisy = [] {
    VerdictEvidence evidence = s_good;
    evidence.within_run_cv_ppm = 60000;
    return evidence;
}();
static_assert(!is_evidence_sound(s_noisy));
static_assert(audit_evidence(s_noisy) == EvidenceFault::WithinRunCvTooHigh);
static_assert(derive_confidence(s_noisy, s_fit) == Confidence::Unknown);

// Out-of-order quantiles are a broken sampler, not a fast tail.
inline constexpr VerdictEvidence s_inverted{
    .quantiles = cog::LatencyQuantiles{40u, 24u, 20u},
    .sample_count = 100000,
    .within_run_cv_ppm = 1000,
    .run_to_run_spread_ppm = 1000,
};
static_assert(!is_evidence_sound(s_inverted));
static_assert(audit_evidence(s_inverted) == EvidenceFault::QuantilesOutOfOrder);
static_assert(audit_evidence(s_good) == EvidenceFault::None);

// A run-to-run spread over the bar is its own fault, distinct from a noisy
// single run. The two fail for different reasons and a reader chasing one
// must not be shown the other.
inline constexpr VerdictEvidence s_drifting = [] {
    VerdictEvidence evidence = s_good;
    evidence.run_to_run_spread_ppm = 150000;
    return evidence;
}();
static_assert(audit_evidence(s_drifting) == EvidenceFault::RunToRunSpreadTooHigh);

// A sub-nanosecond result rounded to zero is a timer that did not run.
inline constexpr VerdictEvidence s_zero_median{
    .quantiles = cog::LatencyQuantiles{0u, 0u, 0u},
    .sample_count = 100000,
    .within_run_cv_ppm = 1000,
    .run_to_run_spread_ppm = 1000,
};
static_assert(audit_evidence(s_zero_median) == EvidenceFault::ZeroMedian);

// The write gate: Unknown cannot become an entry.
static_assert(!admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_noisy, s_fit, 1000u).has_value());
static_assert(admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_noisy, s_fit, 1000u).error()
              == LedgerError::ConfidenceBelowBar);
static_assert(admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_fit, 1000u).has_value());
static_assert(admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_fit, 1000u)->confidence
              == Confidence::High);
static_assert(admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_unfit, 1000u)->confidence
              == Confidence::Low);

// A measurement with no timestamp cannot be aged, so it is refused rather
// than stored with an age of zero that would read as brand new forever.
static_assert(admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_fit, 0u).error()
              == LedgerError::ClockUnavailable);

// Expiry, including the backwards-clock case.
inline constexpr LedgerEntry s_entry =
    *admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_fit, 10000u);
static_assert(!s_entry.is_expired_at(10000u));
static_assert(!s_entry.is_expired_at(10000u + kDefaultTtlSeconds - 1u));
static_assert(s_entry.is_expired_at(10000u + kDefaultTtlSeconds));
static_assert(s_entry.is_expired_at(9999u), "a clock that went backwards forces a remeasure");
static_assert(s_entry.is_servable_at(10000u));
static_assert(!s_entry.is_servable_at(10000u + kDefaultTtlSeconds));

// A low-confidence entry is never servable, fresh or not.
inline constexpr LedgerEntry s_low_entry =
    *admit_entry(VerdictId::TimerFloorNanos, VerdictValue{20u}, s_good, s_unfit, 10000u);
static_assert(!s_low_entry.is_expired_at(10000u));
static_assert(!s_low_entry.is_servable_at(10000u));
static_assert(s_low_entry.competence_defects_at_measurement != 0u);

// The read gate. An unknown lookup hands back exactly what the caller
// named and nothing else.
inline constexpr VerdictLookup s_miss = VerdictLookup::unknown(LedgerError::FingerprintMismatch);
static_assert(!s_miss.is_known());
static_assert(s_miss.value_or_conservative(VerdictValue{999u}) == VerdictValue{999u});
static_assert(s_miss.boolean_or_conservative(false) == false);
static_assert(s_miss.confidence() == Confidence::Unknown);
static_assert(s_miss.miss_reason() == LedgerError::FingerprintMismatch);

inline constexpr VerdictLookup s_hit = VerdictLookup::known(s_entry);
static_assert(s_hit.is_known());
static_assert(s_hit.value_or_conservative(VerdictValue{999u}) == VerdictValue{20u});

// A never-expiring verdict, which is what a geometry answer wants.
static_assert(VerdictTtl::never().never_expires());
static_assert(!VerdictTtl::of_seconds(1u).never_expires());

static_assert(verdict_id_from_name("timer_floor_ns").value() == VerdictId::TimerFloorNanos);
static_assert(!verdict_id_from_name("no_such_verdict").has_value());

}  // namespace verdict_detail::self_test

}  // namespace crucible::ledger
