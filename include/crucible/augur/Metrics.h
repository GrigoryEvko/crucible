#pragma once

// ── crucible::augur — SWMR metrics publication surface ───────────────
//
// Augur publishes latest-value metric samples from one writer thread and
// Keeper / Canopy readers observe snapshots through the SwmrSession role
// surface.  The payload is intentionally fixed-size and trivially
// copyable so AtomicSnapshot can publish it without heap ownership or
// span lifetime hazards.

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

namespace crucible::augur {

struct AugurWriterTag {};
struct AugurReaderTag {};

struct AugurMetrics {
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

using AugurMetricsSample = ::crucible::safety::Stale<AugurMetrics>;
using AugurMetricsComputation =
    ::crucible::effects::Computation<
        ::crucible::effects::Row<::crucible::effects::Effect::Bg>,
        AugurMetrics>;
using AugurMetricsChannel =
    ::crucible::safety::proto::swmr_session::SwmrSession<
        AugurMetricsSample, AugurWriterTag, AugurReaderTag>;
using AugurMetricsWriter = typename AugurMetricsChannel::WriterHandle;
using AugurMetricsReader = typename AugurMetricsChannel::ReaderHandle;

static_assert(std::is_trivially_copyable_v<AugurMetrics>);
static_assert(std::is_trivially_destructible_v<AugurMetrics>);
static_assert(::crucible::concurrent::SnapshotValue<AugurMetricsSample>);

[[nodiscard]] inline AugurMetricsSample
fresh_metrics_sample(AugurMetrics metrics) noexcept {
    return AugurMetricsSample::fresh(metrics);
}

[[nodiscard]] inline AugurMetricsSample
metrics_sample_at(AugurMetrics metrics, std::uint64_t staleness) noexcept {
    return AugurMetricsSample::at(metrics, staleness);
}

[[nodiscard]] inline AugurMetricsWriter mint_augur_metrics_writer(
    AugurMetricsChannel& channel,
    ::crucible::safety::Permission<AugurWriterTag>&& permission) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_writer<
        AugurMetricsChannel>(channel, std::move(permission));
}

[[nodiscard]] inline std::optional<AugurMetricsReader>
mint_keeper_metrics_reader(AugurMetricsChannel& channel) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_reader<
        AugurMetricsChannel>(channel);
}

[[nodiscard]] inline std::optional<AugurMetricsReader>
mint_canopy_metrics_reader(AugurMetricsChannel& channel) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_reader<
        AugurMetricsChannel>(channel);
}

}  // namespace crucible::augur

