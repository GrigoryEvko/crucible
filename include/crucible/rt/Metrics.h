#pragma once

// ── crucible::rt — SWMR metrics publication surface ───────────────
//
// Runtime code publishes latest-value metric samples from one writer thread;
// Keeper / Canopy readers observe snapshots through the SwmrSession role
// surface. The payload is intentionally fixed-size and trivially copyable so
// AtomicSnapshot can publish it without heap ownership or span lifetime
// hazards.

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/effects/Computation.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Stale.h>
#include <crucible/sessions/SwmrSession.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::rt {

struct RuntimeMetricsWriterTag {};
struct RuntimeMetricsReaderTag {};

struct RuntimeMetrics {
    double meb_lambda_max = 0.0;
    double meb_threshold = 0.0;
    double wasserstein_ratio = 0.0;
    double bits_per_step_ratio = 0.0;
    double dmft_tail_fraction = 0.0;
    double ntk_alpha = 0.0;
    double ntk_alpha_drift = 0.0;
    std::uint32_t delta_g_count = 0;
    std::uint32_t reserved = 0;
    std::array<double, 16> delta_g{};
};

using RuntimeMetricsSample = ::crucible::safety::Stale<RuntimeMetrics>;
using RuntimeMetricsComputation =
    ::crucible::effects::Computation<
        ::crucible::effects::Row<::crucible::effects::Effect::Bg>,
        RuntimeMetrics>;
using RuntimeMetricsChannel =
    ::crucible::safety::proto::swmr_session::SwmrSession<
        RuntimeMetricsSample, RuntimeMetricsWriterTag, RuntimeMetricsReaderTag>;
using RuntimeMetricsWriter = typename RuntimeMetricsChannel::WriterHandle;
using RuntimeMetricsReader = typename RuntimeMetricsChannel::ReaderHandle;

static_assert(std::is_trivially_copyable_v<RuntimeMetrics>);
static_assert(std::is_trivially_destructible_v<RuntimeMetrics>);
static_assert(::crucible::concurrent::SnapshotValue<RuntimeMetricsSample>);

[[nodiscard]] inline RuntimeMetricsSample
fresh_metrics_sample(RuntimeMetrics metrics) noexcept {
    return RuntimeMetricsSample::fresh(metrics);
}

[[nodiscard]] inline RuntimeMetricsSample
metrics_sample_at(RuntimeMetrics metrics, std::uint64_t staleness) noexcept {
    return RuntimeMetricsSample::at(metrics, staleness);
}

[[nodiscard]] inline RuntimeMetricsWriter mint_rt_metrics_writer(
    RuntimeMetricsChannel& channel,
    ::crucible::safety::Permission<RuntimeMetricsWriterTag>&& permission) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_writer<
        RuntimeMetricsChannel>(channel, std::move(permission));
}

[[nodiscard]] inline std::optional<RuntimeMetricsReader>
mint_keeper_metrics_reader(RuntimeMetricsChannel& channel) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_reader<
        RuntimeMetricsChannel>(channel);
}

[[nodiscard]] inline std::optional<RuntimeMetricsReader>
mint_canopy_metrics_reader(RuntimeMetricsChannel& channel) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_reader<
        RuntimeMetricsChannel>(channel);
}

}  // namespace crucible::rt
