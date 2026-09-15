#pragma once

// One measured answer, and everything needed to decide whether to believe it.
//
// The rule this header enforces, in one sentence: a verdict that does not
// clear the confidence bar is not a verdict, and a reader that asks for one
// gets back the conservative answer it named at the call site.
//
// That rule is encoded twice, on purpose.
//
//   Write side — admit_entry refuses to build a LedgerEntry whose evidence
//   derives Confidence::Unknown. There is no other constructor. A caller
//   cannot hand the store something the store would have to judge, because
//   the judging already happened and the unjudgeable never became an entry.
//
//   Read side — a lookup returns VerdictLookup, which has no value()
//   accessor at all. The only way to get a number out is
//   value_or_conservative(x), which forces the caller to write down the
//   answer it will use when the ledger has nothing. The fast path is opt-in
//   and the conservative path is what falls out of doing nothing.
//
// Evidence is kept alongside the value rather than collapsed into it. A
// verdict that says "84 ns" tells a reader nothing about whether to act; a
// verdict that says "84 ns, p50 of 100 000 samples, 1.2% within-run cv,
// 0.4% run-to-run spread, on a host with no competence defects" does. When
// a verdict later turns out to be wrong, the evidence is what says why.
//
// DetSafe (axiom 8): a verdict may change how fast something runs and must
// never change what it computes. Nothing here is reachable from
// content_hash, merkle_hash or the memory plan, and
// scripts/check-detsafe-ledger.sh asserts it stays that way.

#include <crucible/cog/Calibrate.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/ledger/Competence.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::ledger {

// ── What is being answered ────────────────────────────────────────────
//
// Frozen by value. A stored entry carries the raw number, so renumbering an
// enumerator silently reinterprets every ledger on every host in the fleet.
// New questions take the next free value. Nothing is ever removed; a
// retired question keeps its number and stops being written.
enum class VerdictId : std::uint16_t {
    // The trivial probe. It measures the cost of reading the cycle counter
    // twice with nothing in between, which is a real number, is stable, and
    // is useless for any decision. It exists to prove the loop closes:
    // measure, judge, store, read back, serve. Real questions are #68-#70.
    TimerFloorNanos = 0,
};

inline constexpr std::uint16_t kVerdictIdCount = 1;

enum class VerdictUnit : std::uint8_t {
    Nanoseconds = 0,
    Bytes = 1,
    Boolean = 2,
    Count = 3,
};

[[nodiscard]] constexpr std::string_view verdict_unit_name(VerdictUnit unit) noexcept {
    switch (unit) {
        case VerdictUnit::Nanoseconds:
            return "ns";
        case VerdictUnit::Bytes:
            return "bytes";
        case VerdictUnit::Boolean:
            return "bool";
        case VerdictUnit::Count:
            return "count";
        default:
            return "<unknown VerdictUnit>";
    }
}

// ── Time to live ──────────────────────────────────────────────────────
//
// An hour by default, because most of what a probe measures is a property
// of a machine under load and load moves. Per-verdict overridable in both
// directions: a cache knee is fixed by geometry the fingerprint already
// folds, so it never expires; a transparent-hugepage collapse cost depends
// on how fragmented memory happens to be, so it expires sooner than an hour.

struct VerdictTtl {
    // Zero is the never-expires sentinel rather than expire-immediately.
    // Immediate expiry is expressible as one second and is not a thing any
    // verdict wants, whereas never-expires is what every geometry verdict
    // wants, so the cheaper spelling goes to the common case.
    std::uint32_t seconds = 0;

    [[nodiscard]] static constexpr VerdictTtl never() noexcept { return VerdictTtl{0u}; }
    [[nodiscard]] static constexpr VerdictTtl of_seconds(std::uint32_t value) noexcept { return VerdictTtl{value}; }
    [[nodiscard]] constexpr bool never_expires() const noexcept { return seconds == 0u; }
    [[nodiscard]] friend constexpr bool operator==(VerdictTtl, VerdictTtl) noexcept = default;
};

inline constexpr std::uint32_t kDefaultTtlSeconds = 3600;

// ── The value ─────────────────────────────────────────────────────────
//
// One unsigned word, with the unit declared by the id's trait rather than
// carried per-instance. A strong type because a bare uint64 crossing a
// boundary is a nanosecond count or a byte count or a boolean depending on
// nothing the compiler can see.

struct VerdictValue {
    std::uint64_t raw_value = 0;

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return raw_value; }
    [[nodiscard]] constexpr bool as_boolean() const noexcept { return raw_value != 0u; }
    [[nodiscard]] static constexpr VerdictValue of_boolean(bool answer) noexcept {
        return VerdictValue{answer ? 1ull : 0ull};
    }
    [[nodiscard]] friend constexpr bool operator==(VerdictValue, VerdictValue) noexcept = default;
};

static_assert(sizeof(VerdictValue) == sizeof(std::uint64_t));

struct VerdictTrait {
    std::string_view name{};
    VerdictUnit unit = VerdictUnit::Count;
    VerdictTtl ttl{};
};

[[nodiscard]] constexpr VerdictTrait verdict_trait(VerdictId id) noexcept {
    switch (id) {
        case VerdictId::TimerFloorNanos:
            // An hour. The timer floor is a property of the part and could
            // take the never-expires TTL, but the trivial probe is also the
            // thing a first-time operator watches refresh, and a verdict
            // that never comes back is a poor demonstration of a cache.
            return VerdictTrait{.name = "timer_floor_ns",
                                .unit = VerdictUnit::Nanoseconds,
                                .ttl = VerdictTtl::of_seconds(kDefaultTtlSeconds)};
        // An id outside the enum can only come from a corrupt file or a
        // build mismatch. The empty name is deliberate: nothing resolves
        // to it through verdict_id_from_name, so such an entry can never
        // be read back and never be served.
        default:
            return VerdictTrait{.name = {}, .unit = VerdictUnit::Count, .ttl = VerdictTtl::of_seconds(1u)};
    }
}

[[nodiscard]] constexpr std::string_view verdict_id_name(VerdictId id) noexcept { return verdict_trait(id).name; }

// Parses a name back to its id. Used by the store when reading an entry
// written by a build that may know more ids than this one.
[[nodiscard]] constexpr std::expected<VerdictId, std::monostate> verdict_id_from_name(std::string_view name) noexcept {
    for (std::uint16_t candidate = 0; candidate < kVerdictIdCount; ++candidate) {
        const VerdictId id = static_cast<VerdictId>(candidate);
        if (verdict_trait(id).name == name) {
            return id;
        }
    }
    return std::unexpected(std::monostate{});
}

// ── Confidence ────────────────────────────────────────────────────────

enum class Confidence : std::uint8_t {
    // Never stored and never served. The reader's answer when it has
    // nothing, and the admission's answer when the evidence does not hold
    // up. Zero so a default-constructed anything reads as unknown.
    Unknown = 0,
    // The evidence is internally sound but the host was not fit to measure.
    // Stored with the defect word, served only through an explicit
    // lookup_including_low.
    Low = 1,
    // Sound evidence on a fit host. The only grade the default reader
    // serves.
    High = 2,
};

[[nodiscard]] constexpr std::string_view confidence_name(Confidence confidence) noexcept {
    switch (confidence) {
        case Confidence::Unknown:
            return "unknown";
        case Confidence::Low:
            return "low";
        case Confidence::High:
            return "high";
        default:
            return "<unknown Confidence>";
    }
}

// ── The bars ──────────────────────────────────────────────────────────
//
// Stated as named constants so a verdict that missed one can be pointed at
// the number it missed, and so moving a bar is a visible edit rather than a
// changed digit inside a condition.

// Fewer than this and the quantiles are describing the sampler, not the
// thing sampled. The bench harness's own bootstrap refuses below 30.
inline constexpr std::uint32_t kMinSampleCount = 32;

// The house rule: variance above 5% within a run means the part was
// throttling and the run is void. Parts per million so the comparison is
// integral and reads back from disk identically.
inline constexpr std::uint32_t kMaxWithinRunCvPpm = 50000;  // 5%

// Run-to-run gets a looser bar than within-run because it folds in cold
// caches and scheduler placement that a single run never sees. Ten percent
// is the same bar the bench harness's drift check uses.
inline constexpr std::uint32_t kMaxRunToRunSpreadPpm = 100000;  // 10%

struct VerdictEvidence {
    // The same three-quantile shape cog::OpcodeLatencyEntry carries, so a
    // ledger entry and a calibration entry describe a measurement the same
    // way and one can be built from the other.
    cog::LatencyQuantiles quantiles{};

    // uint32 rather than cog::CalibrationSampleCount. That type is
    // Bounded<1, 65535, uint16_t>, and the bench harness takes 100 000
    // samples by default, so reusing it would silently cap the count at
    // 65 535 and understate the evidence for every well-run probe.
    std::uint32_t sample_count = 0;

    std::uint32_t within_run_cv_ppm = 0;
    std::uint32_t run_to_run_spread_ppm = 0;

    [[nodiscard]] friend constexpr bool operator==(VerdictEvidence const&, VerdictEvidence const&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<VerdictEvidence>);

// Which bar the evidence missed. A bare "refused" is the failure mode this
// whole ledger exists to prevent: a bench harness on this box once collected
// 128 samples instead of 100 000 and said nothing about it, because the only
// thing it reported was that something was off. Every refusal here names the
// bar it missed so the next person does not have to bisect to find out.
enum class EvidenceFault : std::uint8_t {
    None = 0,
    TooFewSamples = 1,
    QuantilesOutOfOrder = 2,
    ZeroMedian = 3,
    WithinRunCvTooHigh = 4,
    RunToRunSpreadTooHigh = 5,
};

[[nodiscard]] constexpr std::string_view evidence_fault_name(EvidenceFault fault) noexcept {
    switch (fault) {
        case EvidenceFault::None:
            return "none";
        case EvidenceFault::TooFewSamples:
            return "TooFewSamples";
        case EvidenceFault::QuantilesOutOfOrder:
            return "QuantilesOutOfOrder";
        case EvidenceFault::ZeroMedian:
            return "ZeroMedian";
        case EvidenceFault::WithinRunCvTooHigh:
            return "WithinRunCvTooHigh";
        case EvidenceFault::RunToRunSpreadTooHigh:
            return "RunToRunSpreadTooHigh";
        default:
            return "<unknown EvidenceFault>";
    }
}

// Is the evidence internally sound, setting the host aside? Sound means
// enough samples, quantiles in order, and both spreads inside their bars.
[[nodiscard]] constexpr EvidenceFault audit_evidence(VerdictEvidence const& evidence) noexcept {
    if (evidence.sample_count < kMinSampleCount) {
        return EvidenceFault::TooFewSamples;
    }
    // cog's own ordering predicate, so the ledger and the calibration
    // schema cannot come to different conclusions about the same triple.
    if (!cog::quantile_ordered(evidence.quantiles)) {
        return EvidenceFault::QuantilesOutOfOrder;
    }
    // A p50 of zero is a timer that did not run, not a free operation.
    if (evidence.quantiles.p50_ns == 0u) {
        return EvidenceFault::ZeroMedian;
    }
    if (evidence.within_run_cv_ppm > kMaxWithinRunCvPpm) {
        return EvidenceFault::WithinRunCvTooHigh;
    }
    if (evidence.run_to_run_spread_ppm > kMaxRunToRunSpreadPpm) {
        return EvidenceFault::RunToRunSpreadTooHigh;
    }
    return EvidenceFault::None;
}

[[nodiscard]] constexpr bool is_evidence_sound(VerdictEvidence const& evidence) noexcept {
    return audit_evidence(evidence) == EvidenceFault::None;
}

// The whole fail-closed rule in one function. High needs sound evidence AND
// a fit host. Low is sound evidence on an unfit one. Anything else is
// Unknown, and Unknown is never stored.
[[nodiscard]] constexpr Confidence derive_confidence(VerdictEvidence const& evidence,
                                                     CompetenceReport const& competence) noexcept {
    if (!is_evidence_sound(evidence)) {
        return Confidence::Unknown;
    }
    return competence.is_competent() ? Confidence::High : Confidence::Low;
}

// ── The entry ─────────────────────────────────────────────────────────

enum class LedgerError : std::uint8_t {
    None = 0,
    ConfidenceBelowBar = 1,  // the evidence derived Unknown
    UnknownVerdictId = 2,
    MalformedRecord = 3,
    FingerprintMismatch = 4,
    StorePathUnavailable = 5,
    StoreReadFailed = 6,
    StoreWriteFailed = 7,
    StoreCommitFailed = 8,
    StoreFull = 9,
    DuplicateVerdict = 10,
    ClockUnavailable = 11,
};

[[nodiscard]] constexpr std::string_view ledger_error_name(LedgerError error) noexcept {
    switch (error) {
        case LedgerError::None:
            return "None";
        case LedgerError::ConfidenceBelowBar:
            return "ConfidenceBelowBar";
        case LedgerError::UnknownVerdictId:
            return "UnknownVerdictId";
        case LedgerError::MalformedRecord:
            return "MalformedRecord";
        case LedgerError::FingerprintMismatch:
            return "FingerprintMismatch";
        case LedgerError::StorePathUnavailable:
            return "StorePathUnavailable";
        case LedgerError::StoreReadFailed:
            return "StoreReadFailed";
        case LedgerError::StoreWriteFailed:
            return "StoreWriteFailed";
        case LedgerError::StoreCommitFailed:
            return "StoreCommitFailed";
        case LedgerError::StoreFull:
            return "StoreFull";
        case LedgerError::DuplicateVerdict:
            return "DuplicateVerdict";
        case LedgerError::ClockUnavailable:
            return "ClockUnavailable";
        default:
            return "<unknown LedgerError>";
    }
}

struct LedgerEntry {
    VerdictId id = VerdictId::TimerFloorNanos;

    // Tagged Calibrated for the same reason cog::TargetCaps tags its
    // measured fields: a reader that sees an untagged number cannot tell a
    // vendor datasheet figure from something this host actually did.
    safety::Tagged<VerdictValue, safety::source::Calibrated> value{VerdictValue{}};

    Confidence confidence = Confidence::Unknown;
    VerdictEvidence evidence{};

    // The competence word AS IT WAS when the measurement ran, not as it is
    // now. A host that was degraded at measurement time produced a degraded
    // number, and tidying the host afterwards does not retroactively clean
    // the number.
    std::uint16_t competence_defects_at_measurement = 0;

    std::uint64_t measured_at_unix_seconds = 0;
    VerdictTtl ttl{};

    [[nodiscard]] constexpr bool is_expired_at(std::uint64_t now_unix_seconds) const noexcept {
        if (ttl.never_expires()) {
            return false;
        }
        // A clock that moved backwards leaves now < measured_at. Treating
        // that as expired is the conservative reading: it forces a
        // remeasure rather than trusting an entry whose age cannot be
        // computed.
        if (now_unix_seconds < measured_at_unix_seconds) {
            return true;
        }
        return (now_unix_seconds - measured_at_unix_seconds) >= static_cast<std::uint64_t>(ttl.seconds);
    }

    [[nodiscard]] constexpr bool is_servable_at(std::uint64_t now_unix_seconds) const noexcept {
        return confidence == Confidence::High && !is_expired_at(now_unix_seconds);
    }
};

static_assert(std::is_trivially_copyable_v<LedgerEntry>);

// The only way to make a LedgerEntry that carries a judgement. Nothing else
// in the ledger sets `confidence`, so an Unknown verdict has no path into
// the store.
[[nodiscard]] constexpr std::expected<LedgerEntry, LedgerError>
admit_entry(VerdictId id, VerdictValue value, VerdictEvidence const& evidence, CompetenceReport const& competence,
            std::uint64_t measured_at_unix_seconds) noexcept {
    const Confidence confidence = derive_confidence(evidence, competence);
    if (confidence == Confidence::Unknown) {
        return std::unexpected(LedgerError::ConfidenceBelowBar);
    }
    if (measured_at_unix_seconds == 0u) {
        return std::unexpected(LedgerError::ClockUnavailable);
    }
    return LedgerEntry{
        .id = id,
        .value = safety::Tagged<VerdictValue, safety::source::Calibrated>{value},
        .confidence = confidence,
        .evidence = evidence,
        .competence_defects_at_measurement = competence.defect_word(),
        .measured_at_unix_seconds = measured_at_unix_seconds,
        .ttl = verdict_trait(id).ttl,
    };
}

// ── The read-side answer ──────────────────────────────────────────────
//
// Deliberately has no value() and no operator*. A caller that wants a
// number must name the number it will use when there is none, in the same
// expression. That makes the conservative path the one you get by writing
// the obvious thing, and makes every site where a measured value is trusted
// greppable by its conservative fallback.

class VerdictLookup {
public:
    constexpr VerdictLookup() noexcept = default;

    [[nodiscard]] static constexpr VerdictLookup unknown(LedgerError why) noexcept {
        VerdictLookup lookup{};
        lookup.miss_reason_ = why;
        return lookup;
    }

    [[nodiscard]] static constexpr VerdictLookup known(LedgerEntry const& entry) noexcept {
        VerdictLookup lookup{};
        lookup.is_known_ = true;
        lookup.value_ = entry.value.value();
        lookup.confidence_ = entry.confidence;
        lookup.evidence_ = entry.evidence;
        lookup.measured_at_unix_seconds_ = entry.measured_at_unix_seconds;
        return lookup;
    }

    [[nodiscard]] constexpr bool is_known() const noexcept { return is_known_; }

    // The only accessor that yields a number. The argument is not a
    // convenience: it is the conservative answer, and requiring it at every
    // call site is what makes a missing verdict safe by construction.
    [[nodiscard]] constexpr VerdictValue value_or_conservative(VerdictValue conservative) const noexcept {
        return is_known_ ? value_ : conservative;
    }

    [[nodiscard]] constexpr bool boolean_or_conservative(bool conservative) const noexcept {
        return is_known_ ? value_.as_boolean() : conservative;
    }

    [[nodiscard]] constexpr Confidence confidence() const noexcept { return confidence_; }
    [[nodiscard]] constexpr VerdictEvidence evidence() const noexcept { return evidence_; }
    [[nodiscard]] constexpr std::uint64_t measured_at_unix_seconds() const noexcept {
        return measured_at_unix_seconds_;
    }

    // Why there was nothing to serve. Meaningful only when is_known() is
    // false; a hit leaves it None.
    [[nodiscard]] constexpr LedgerError miss_reason() const noexcept { return miss_reason_; }

private:
    bool is_known_ = false;
    VerdictValue value_{};
    Confidence confidence_ = Confidence::Unknown;
    VerdictEvidence evidence_{};
    std::uint64_t measured_at_unix_seconds_ = 0;
    LedgerError miss_reason_ = LedgerError::None;
};

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
