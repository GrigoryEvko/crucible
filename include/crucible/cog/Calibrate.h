#pragma once

// GAPS-196. Typed per-Cog calibration substrate.
//
// This header owns the shape of calibration plans, measured samples, and
// calibrated result bundles. It deliberately does not invent hardware
// measurements: live vendor microbenchmark runners are backend work and return
// BackendUnavailable until a real Mimic/Cog runner supplies samples.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cog {

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

[[nodiscard]] std::string_view
calibration_error_name(CalibrationError error) noexcept;

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

using CalibrationIterations =
    safety::Bounded<std::uint32_t{1}, std::uint32_t{65'535}, std::uint32_t>;
using WarmupIterations =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{65'535}, std::uint32_t>;
using TrimBasisPoints =
    safety::Bounded<std::uint16_t{0}, std::uint16_t{1'000}, std::uint16_t>;
using RuntimeBudgetMs =
    safety::Bounded<std::uint32_t{1}, std::uint32_t{86'400'000}, std::uint32_t>;
using CalibrationSampleCount =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{65'535}, std::uint16_t>;
using DriftBasisPoints =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{10'000}, std::uint16_t>;

inline constexpr auto calibration_quantiles_valid =
    [](LatencyQuantiles q) constexpr noexcept {
        return q.p50_ns > 0u
            && q.p50_ns <= q.p99_ns
            && q.p99_ns <= q.p999_ns;
    };

using CalibrationLatencyQuantiles =
    safety::Refined<calibration_quantiles_valid, LatencyQuantiles>;

inline constexpr auto finite_positive_throughput =
    [](double value) constexpr noexcept {
        return value > 0.0 && value <= 1.0e30;
    };

using CalibratedThroughput =
    safety::Refined<finite_positive_throughput, double>;

struct CalibrationPlan {
    CalibrationIterations iterations{std::uint32_t{1000}};
    WarmupIterations warmup_iterations{std::uint32_t{100}};
    TrimBasisPoints trim_basis_points{std::uint16_t{100}};
    RuntimeBudgetMs runtime_budget_ms{std::uint32_t{60'000}};
    CalibrationTrigger trigger = CalibrationTrigger::Startup;
    CalibrationBackend backend = CalibrationBackend::VendorMimic;
    bool require_thermal_stability = true;
};

struct DriftSignal {
    DriftBasisPoints observed_drift_bps{std::uint16_t{1}};
    DriftBasisPoints threshold_bps{std::uint16_t{1000}};
};

template <CogKind K>
concept CalibratableCogKind = HasCaps<K> && HasOpcodeTable<K>;

template <class Ctx>
concept CtxFitsCalibration =
    effects::IsExecCtx<Ctx>
    && (effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>
        || effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>);

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
    CalibrationLatencyQuantiles latency{LatencyQuantiles{1u, 1u, 1u}};
    CalibratedThroughput throughput_per_sec{1.0};
    CalibrationSampleCount sample_count{std::uint16_t{1}};
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
    safety::Tagged<std::uint16_t, safety::source::Calibrated>
        entry_count{std::uint16_t{0}};
};

[[nodiscard]] constexpr std::expected<CalibrationIterations, CalibrationError>
admit_calibration_iterations(std::uint32_t iterations) noexcept {
    if (iterations == 0u || iterations > 65'535u) {
        return std::unexpected(CalibrationError::InvalidIterations);
    }
    return CalibrationIterations{
        iterations, typename CalibrationIterations::Trusted{}};
}

[[nodiscard]] constexpr std::expected<WarmupIterations, CalibrationError>
admit_warmup_iterations(std::uint32_t iterations) noexcept {
    if (iterations > 65'535u) {
        return std::unexpected(CalibrationError::InvalidWarmupIterations);
    }
    return WarmupIterations{
        iterations, typename WarmupIterations::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TrimBasisPoints, CalibrationError>
admit_trim_basis_points(std::uint16_t basis_points) noexcept {
    if (basis_points > 1'000u) {
        return std::unexpected(CalibrationError::InvalidTrimBasisPoints);
    }
    return TrimBasisPoints{
        basis_points, typename TrimBasisPoints::Trusted{}};
}

[[nodiscard]] constexpr std::expected<RuntimeBudgetMs, CalibrationError>
admit_runtime_budget_ms(std::uint32_t runtime_ms) noexcept {
    if (runtime_ms == 0u || runtime_ms > 86'400'000u) {
        return std::unexpected(CalibrationError::InvalidRuntimeBudgetMs);
    }
    return RuntimeBudgetMs{runtime_ms, typename RuntimeBudgetMs::Trusted{}};
}

[[nodiscard]] constexpr std::expected<CalibrationSampleCount, CalibrationError>
admit_sample_count(std::uint32_t samples) noexcept {
    if (samples == 0u || samples > 65'535u) {
        return std::unexpected(CalibrationError::InvalidSampleCount);
    }
    return CalibrationSampleCount{
        static_cast<std::uint16_t>(samples),
        typename CalibrationSampleCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<CalibrationLatencyQuantiles,
                                      CalibrationError>
admit_latency_quantiles(LatencyQuantiles q) noexcept {
    if (!calibration_quantiles_valid(q)) {
        return std::unexpected(CalibrationError::InvalidLatencyQuantiles);
    }
    return CalibrationLatencyQuantiles{
        q, typename CalibrationLatencyQuantiles::Trusted{}};
}

[[nodiscard]] constexpr std::expected<CalibratedThroughput, CalibrationError>
admit_throughput_per_sec(double throughput) noexcept {
    if (!finite_positive_throughput(throughput)) {
        return std::unexpected(CalibrationError::InvalidThroughput);
    }
    return CalibratedThroughput{
        throughput, typename CalibratedThroughput::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DriftBasisPoints, CalibrationError>
admit_drift_basis_points(std::uint16_t basis_points) noexcept {
    if (basis_points == 0u || basis_points > 10'000u) {
        return std::unexpected(CalibrationError::InvalidDriftBasisPoints);
    }
    return DriftBasisPoints{
        basis_points, typename DriftBasisPoints::Trusted{}};
}

[[nodiscard]] constexpr bool
should_recalibrate(DriftSignal signal) noexcept {
    return signal.observed_drift_bps.value() >= signal.threshold_bps.value();
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<void, CalibrationError>
validate_identity_for(CogIdentity identity) noexcept {
    if (identity.uuid.is_zero()) {
        return std::unexpected(CalibrationError::ZeroCog);
    }
    if (identity.kind != K) {
        return std::unexpected(CalibrationError::KindMismatch);
    }
    return {};
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr OpcodeLatencyEntry<K>
make_latency_entry(CalibrationSample<K> sample) noexcept {
    return OpcodeLatencyEntry<K>{
        .opcode = sample.opcode,
        .size_bucket = sample.size_bucket,
        .dtype_bucket = sample.dtype_bucket,
        .transpose_mode = sample.transpose_mode,
        .message_size_bucket = sample.message_size_bucket,
        .latency_cycles = sample.latency_cycles,
        .latency = OrderedLatencyQuantiles{
            sample.latency.value(), typename OrderedLatencyQuantiles::Trusted{}},
        .throughput_per_sec = sample.throughput_per_sec.value(),
        .sample_count = safety::Tagged<std::uint16_t,
                                       safety::source::Calibrated>{
            sample.sample_count.value()},
    };
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<void, CalibrationError>
validate_latency_entry(OpcodeLatencyEntry<K> const& entry) noexcept {
    if (entry.latency_cycles == 0u) {
        return std::unexpected(CalibrationError::InvalidLatencyCycles);
    }
    const LatencyQuantiles q = entry.latency.value();
    if (!calibration_quantiles_valid(q)) {
        return std::unexpected(CalibrationError::InvalidLatencyQuantiles);
    }
    if (!finite_positive_throughput(entry.throughput_per_sec)) {
        return std::unexpected(CalibrationError::InvalidThroughput);
    }
    if (entry.sample_count.value() == 0u) {
        return std::unexpected(CalibrationError::InvalidSampleCount);
    }
    return {};
}

template <CogKind K>
    requires CalibratableCogKind<K>
[[nodiscard]] constexpr std::expected<CalibrationResult<K>, CalibrationError>
build_calibration_result(CogIdentity identity,
                         caps_for_t<K> caps,
                         std::span<const OpcodeLatencyEntry<K>> entries,
                         CalibrationPlan plan = {}) noexcept {
    auto valid_identity = validate_identity_for<K>(identity);
    if (!valid_identity.has_value()) {
        return std::unexpected(valid_identity.error());
    }
    if (entries.empty()) {
        return std::unexpected(CalibrationError::EmptyEntrySet);
    }
    if (entries.size() > 65'535u) {
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
        .opcode_table = OpcodeLatencyTable<K>{
            .entries = safety::Tagged<std::span<const OpcodeLatencyEntry<K>>,
                                      safety::source::Calibrated>{entries},
            .calibration_age_seconds = safety::Stale<double>::fresh(0.0),
        },
        .plan = plan,
        .entry_count = safety::Tagged<std::uint16_t,
                                      safety::source::Calibrated>{
            static_cast<std::uint16_t>(entries.size())},
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
calibrate_specific_opcodes(Ctx const&,
                           CogIdentity identity,
                           std::span<const opcodes_for_t<K>> opcodes,
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
recalibrate_drifted(Ctx const& ctx,
                    CogIdentity identity,
                    DriftSignal drift,
                    CalibrationPlan plan = {}) noexcept {
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
static_assert(CtxFitsCalibration<effects::ColdInitCtx>);
static_assert(CtxFitsCalibration<effects::BgDrainCtx>);
static_assert(!CtxFitsCalibration<effects::HotFgCtx>);
static_assert(std::is_trivially_copyable_v<CalibrationPlan>);
static_assert(std::is_trivially_copyable_v<DriftSignal>);

}  // namespace crucible::cog
