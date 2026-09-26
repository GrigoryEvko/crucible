#pragma once

// The shape of a calibration plan, of one measured sample and of a
// finished result, together with the admission that guards each. No
// measurement happens here. A function that would need a live
// microbenchmark runner reports BackendUnavailable until one exists,
// so the only way to obtain a result today is to build one from
// samples a caller supplies.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::cog {

// ::foundation::reflect::enum_name gives the name of an enumerator.
enum class CalibrationError : std::uint8_t {
    None = 0,
    ZeroCog = 1,
    KindMismatch = 2,
    NonCalibratableCog = 3,
    InvalidIterations = 4,
    InvalidWarmupIterations = 5,
    InvalidTrimBasisPoints = 6,
    InvalidRuntimeBudgetMs = 7,
    InvalidLatencyQuantiles = 8,
    InvalidLatencyCycles = 9,
    InvalidThroughput = 10,
    InvalidSampleCount = 11,
    EmptyOpcodeSet = 12,
    EmptyEntrySet = 13,
    DriftBelowThreshold = 14,
    BackendUnavailable = 15,
    InvalidDriftBasisPoints = 16,
};

enum class CalibrationTrigger : std::uint8_t {
    Startup = 0,
    ScheduledRefresh = 1,
    Drift = 2,
    OperatorRequest = 3,
};

enum class CalibrationBackend : std::uint8_t {
    CpuOracle = 0,
    VendorMimic = 1,
    NicLoopback = 2,
    SwitchProbe = 3,
};

// Each refinement below names its predicate, so a site that builds one
// writes ::fixy::mint_refined<predicate>(value) and the check runs there.

inline constexpr auto calibration_iterations_bound = ::fixy::in_range<std::uint32_t{1}, std::uint32_t{65535}>;
using CalibrationIterations = ::fixy::Refined<calibration_iterations_bound, std::uint32_t>;

inline constexpr auto warmup_iterations_bound = ::fixy::bounded_above<std::uint32_t{65535}>;
using WarmupIterations = ::fixy::Refined<warmup_iterations_bound, std::uint32_t>;

// Basis points of the sample tails that the trimmed mean drops: at most
// ten percent.
inline constexpr auto trim_basis_points_bound = ::fixy::bounded_above<std::uint16_t{1000}>;
using TrimBasisPoints = ::fixy::Refined<trim_basis_points_bound, std::uint16_t>;

// One millisecond to one day.
inline constexpr auto runtime_budget_ms_bound = ::fixy::in_range<std::uint32_t{1}, std::uint32_t{86400000}>;
using RuntimeBudgetMs = ::fixy::Refined<runtime_budget_ms_bound, std::uint32_t>;

// A sample count of zero means the row was never measured, so a measured
// sample carries at least one.
inline constexpr auto measured_sample_count = ::fixy::positive;
using CalibrationSampleCount = ::fixy::Refined<measured_sample_count, std::uint16_t>;

inline constexpr auto drift_basis_points_bound = ::fixy::in_range<std::uint16_t{1}, std::uint16_t{10000}>;
using DriftBasisPoints = ::fixy::Refined<drift_basis_points_bound, std::uint16_t>;

// A p50 of zero is a timer that did not run, not a free operation. The
// ledger reads the same predicate, so the ledger and the calibration
// schema cannot come to different conclusions about the same triple.
struct IsMedianPositive {
    [[nodiscard]] constexpr bool operator()(const LatencyQuantiles& q) const noexcept { return q.p50_ns > 0u; }
};

inline constexpr IsMedianPositive median_positive{};

// A measured triple is ordered, as every stored triple is, and has a
// positive median.
inline constexpr auto calibration_quantiles_valid = ::fixy::all_of<quantile_ordered, median_positive>;
using CalibrationLatencyQuantiles = ::fixy::Refined<calibration_quantiles_valid, LatencyQuantiles>;

// A NaN fails the positive conjunct and an infinity fails the ceiling.
inline constexpr auto finite_positive_throughput = ::fixy::all_of<::fixy::positive, ::fixy::bounded_above<1.0e30>>;
using CalibratedThroughput = ::fixy::Refined<finite_positive_throughput, double>;

struct CalibrationPlan {
    CalibrationIterations iterations = ::fixy::mint_refined<calibration_iterations_bound>(std::uint32_t{1000});
    WarmupIterations warmup_iterations = ::fixy::mint_refined<warmup_iterations_bound>(std::uint32_t{100});
    TrimBasisPoints trim_basis_points = ::fixy::mint_refined<trim_basis_points_bound>(std::uint16_t{100});
    RuntimeBudgetMs runtime_budget_ms = ::fixy::mint_refined<runtime_budget_ms_bound>(std::uint32_t{60000});
    CalibrationTrigger trigger = CalibrationTrigger::Startup;
    CalibrationBackend backend = CalibrationBackend::VendorMimic;
    bool require_thermal_stability = true;
};

struct DriftSignal {
    DriftBasisPoints observed_drift_bps = ::fixy::mint_refined<drift_basis_points_bound>(std::uint16_t{1});
    DriftBasisPoints threshold_bps = ::fixy::mint_refined<drift_basis_points_bound>(std::uint16_t{1000});
};

template <CogKind K>
concept CalibratableCogKind = HasCaps<K> && HasOpcodeTable<K>;

// Calibration is startup or background work, never the hot path.
template <class Ctx>
concept CtxFitsCalibration =
    ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Init, ::foundation::effects::Effect::Bg>;

template <CogKind K>
    requires CalibratableCogKind<K>
struct CalibrationSample {
    using OpcodeId = opcodes_for_t<K>;

    OpcodeId opcode{};
    SizeBucket size_bucket = SizeBucket::None;
    DtypeBucket dtype_bucket = DtypeBucket::None;
    TransposeMode transpose_mode = TransposeMode::Nn;
    MessageSizeBucket message_size_bucket = MessageSizeBucket::None;
    std::uint32_t latency_cycles = 0;
    CalibrationLatencyQuantiles latency = ::fixy::mint_refined<calibration_quantiles_valid>(LatencyQuantiles{1u, 1u, 1u});
    CalibratedThroughput throughput_per_sec = ::fixy::mint_refined<finite_positive_throughput>(1.0);
    CalibrationSampleCount sample_count = ::fixy::mint_refined<measured_sample_count>(std::uint16_t{1});
};

template <CogKind K>
    requires CalibratableCogKind<K>
struct CalibrationResult {
    using Caps = caps_for_t<K>;
    using Entry = OpcodeLatencyEntry<K>;
    using Table = OpcodeLatencyTable<K>;

    CogIdentity identity{};
    Caps target_caps{};
    Table opcode_table{};
    CalibrationPlan plan{};
    CalibratedValue<std::uint16_t> entry_count{};
};

namespace detail {

// Every admission below is one refinement door: the predicate runs, a
// refused value reports its own error, and an admitted one is minted
// through the checked door.
template <auto Pred, typename T>
[[nodiscard]] constexpr std::expected<::fixy::Refined<Pred, T>, CalibrationError>
admit_refined(T value, CalibrationError refusal) noexcept {
    if (!Pred(value)) {
        return std::unexpected(refusal);
    }
    return ::fixy::mint_refined<Pred>(value);
}

}  // namespace detail

[[nodiscard]] constexpr std::expected<CalibrationIterations, CalibrationError>
admit_calibration_iterations(std::uint32_t iterations) noexcept {
    return detail::admit_refined<calibration_iterations_bound>(iterations, CalibrationError::InvalidIterations);
}

[[nodiscard]] constexpr std::expected<WarmupIterations, CalibrationError>
admit_warmup_iterations(std::uint32_t iterations) noexcept {
    return detail::admit_refined<warmup_iterations_bound>(iterations, CalibrationError::InvalidWarmupIterations);
}

[[nodiscard]] constexpr std::expected<TrimBasisPoints, CalibrationError>
admit_trim_basis_points(std::uint16_t basis_points) noexcept {
    return detail::admit_refined<trim_basis_points_bound>(basis_points, CalibrationError::InvalidTrimBasisPoints);
}

[[nodiscard]] constexpr std::expected<RuntimeBudgetMs, CalibrationError>
admit_runtime_budget_ms(std::uint32_t runtime_ms) noexcept {
    return detail::admit_refined<runtime_budget_ms_bound>(runtime_ms, CalibrationError::InvalidRuntimeBudgetMs);
}

// The count arrives as the width a bench harness counts in, so the range
// check runs before the narrowing and a count above the storage width is
// refused, never truncated.
[[nodiscard]] constexpr std::expected<CalibrationSampleCount, CalibrationError>
admit_sample_count(std::uint32_t samples) noexcept {
    if (samples > std::numeric_limits<std::uint16_t>::max()) {
        return std::unexpected(CalibrationError::InvalidSampleCount);
    }
    return detail::admit_refined<measured_sample_count>(static_cast<std::uint16_t>(samples),
                                                        CalibrationError::InvalidSampleCount);
}

[[nodiscard]] constexpr std::expected<CalibrationLatencyQuantiles, CalibrationError>
admit_latency_quantiles(LatencyQuantiles q) noexcept {
    return detail::admit_refined<calibration_quantiles_valid>(q, CalibrationError::InvalidLatencyQuantiles);
}

[[nodiscard]] constexpr std::expected<CalibratedThroughput, CalibrationError>
admit_throughput_per_sec(double throughput) noexcept {
    return detail::admit_refined<finite_positive_throughput>(throughput, CalibrationError::InvalidThroughput);
}

[[nodiscard]] constexpr std::expected<DriftBasisPoints, CalibrationError>
admit_drift_basis_points(std::uint16_t basis_points) noexcept {
    return detail::admit_refined<drift_basis_points_bound>(basis_points, CalibrationError::InvalidDriftBasisPoints);
}

[[nodiscard]] constexpr bool should_recalibrate(DriftSignal signal) noexcept {
    return signal.observed_drift_bps.value() >= signal.threshold_bps.value();
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<void, CalibrationError> validate_identity_for(CogIdentity identity) noexcept {
    if (identity.uuid.is_zero()) {
        return std::unexpected(CalibrationError::ZeroCog);
    }
    if (identity.kind != K) {
        return std::unexpected(CalibrationError::KindMismatch);
    }
    return {};
}

// The table stores only the ordering half of the calibration predicate,
// so the latency passes through the table's own checked door.
template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr OpcodeLatencyEntry<K> make_latency_entry(CalibrationSample<K> sample) noexcept {
    return OpcodeLatencyEntry<K>{
        .opcode = sample.opcode,
        .size_bucket = sample.size_bucket,
        .dtype_bucket = sample.dtype_bucket,
        .transpose_mode = sample.transpose_mode,
        .message_size_bucket = sample.message_size_bucket,
        .latency_cycles = sample.latency_cycles,
        .latency = ::fixy::mint_refined<quantile_ordered>(sample.latency.value()),
        .throughput_per_sec = sample.throughput_per_sec.value(),
        .sample_count = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(sample.sample_count.value()),
    };
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<void, CalibrationError>
validate_latency_entry(OpcodeLatencyEntry<K> const& entry) noexcept {
    if (entry.latency_cycles == 0u) {
        return std::unexpected(CalibrationError::InvalidLatencyCycles);
    }
    if (!calibration_quantiles_valid(entry.latency.value())) {
        return std::unexpected(CalibrationError::InvalidLatencyQuantiles);
    }
    if (!finite_positive_throughput(entry.throughput_per_sec)) {
        return std::unexpected(CalibrationError::InvalidThroughput);
    }
    if (!measured_sample_count(entry.sample_count.value())) {
        return std::unexpected(CalibrationError::InvalidSampleCount);
    }
    return {};
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<CalibrationResult<K>, CalibrationError>
build_calibration_result(CogIdentity identity, caps_for_t<K> caps, std::span<const OpcodeLatencyEntry<K>> entries,
                         CalibrationPlan plan = {}) noexcept {
    auto valid_identity = validate_identity_for<K>(identity);
    if (!valid_identity.has_value()) {
        return std::unexpected(valid_identity.error());
    }
    if (entries.empty()) {
        return std::unexpected(CalibrationError::EmptyEntrySet);
    }
    if (entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        return std::unexpected(CalibrationError::InvalidSampleCount);
    }
    for (const OpcodeLatencyEntry<K>& entry : entries) {
        auto valid_entry = validate_latency_entry<K>(entry);
        if (!valid_entry.has_value()) {
            return std::unexpected(valid_entry.error());
        }
    }
    return CalibrationResult<K>{
        .identity = identity,
        .target_caps = caps,
        .opcode_table =
            OpcodeLatencyTable<K>{
                .entries = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(entries),
                .calibration_age_seconds = ::fixy::Stale<double>::fresh(0.0),
            },
        .plan = plan,
        .entry_count = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(static_cast<std::uint16_t>(entries.size())),
    };
}

template <CogKind K, class Ctx>
    requires CalibratableCogKind<K> && CtxFitsCalibration<Ctx>
[[nodiscard]] constexpr std::expected<CalibrationResult<K>, CalibrationError>
calibrate_cog(Ctx const&, CogIdentity identity, CalibrationPlan = {}) noexcept {
    auto valid_identity = validate_identity_for<K>(identity);
    if (!valid_identity.has_value()) {
        return std::unexpected(valid_identity.error());
    }
    return std::unexpected(CalibrationError::BackendUnavailable);
}

template <CogKind K, class Ctx>
    requires CalibratableCogKind<K> && CtxFitsCalibration<Ctx>
[[nodiscard]] constexpr std::expected<CalibrationResult<K>, CalibrationError>
calibrate_specific_opcodes(Ctx const&, CogIdentity identity, std::span<const opcodes_for_t<K>> opcodes,
                           CalibrationPlan = {}) noexcept {
    auto valid_identity = validate_identity_for<K>(identity);
    if (!valid_identity.has_value()) {
        return std::unexpected(valid_identity.error());
    }
    if (opcodes.empty()) {
        return std::unexpected(CalibrationError::EmptyOpcodeSet);
    }
    return std::unexpected(CalibrationError::BackendUnavailable);
}

template <CogKind K, class Ctx>
    requires CalibratableCogKind<K> && CtxFitsCalibration<Ctx>
[[nodiscard]] constexpr std::expected<CalibrationResult<K>, CalibrationError>
recalibrate_drifted(Ctx const& ctx, CogIdentity identity, DriftSignal drift, CalibrationPlan plan = {}) noexcept {
    if (!should_recalibrate(drift)) {
        return std::unexpected(CalibrationError::DriftBelowThreshold);
    }
    plan.trigger = CalibrationTrigger::Drift;
    return calibrate_cog<K>(ctx, identity, plan);
}

static_assert(sizeof(CalibrationIterations) == sizeof(std::uint32_t));
static_assert(sizeof(WarmupIterations) == sizeof(std::uint32_t));
static_assert(sizeof(TrimBasisPoints) == sizeof(std::uint16_t));
static_assert(sizeof(RuntimeBudgetMs) == sizeof(std::uint32_t));
static_assert(sizeof(CalibrationSampleCount) == sizeof(std::uint16_t));
static_assert(sizeof(CalibrationLatencyQuantiles) == sizeof(LatencyQuantiles));
static_assert(sizeof(CalibratedThroughput) == sizeof(double));
static_assert(CalibratableCogKind<CogKind::Gpu>);
static_assert(CalibratableCogKind<CogKind::NicPort>);
static_assert(!CalibratableCogKind<CogKind::PsuRail>);
static_assert(CtxFitsCalibration<::fixy::ColdInitCtx>);
static_assert(CtxFitsCalibration<::fixy::BgDrainCtx>);
static_assert(!CtxFitsCalibration<::fixy::HotFgCtx>);
static_assert(!CtxFitsCalibration<::fixy::TestRunnerCtx>);

// A refined member keeps the class from being trivially copyable, by
// design: no byte copy builds a refined value. A plan and a drift signal
// still copy and destroy as plain data.
static_assert(std::is_trivially_copy_constructible_v<CalibrationPlan> && std::is_trivially_destructible_v<CalibrationPlan>);
static_assert(std::is_trivially_copy_constructible_v<DriftSignal> && std::is_trivially_destructible_v<DriftSignal>);

}  // namespace crucible::cog
