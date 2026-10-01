#pragma once

// The runtime metrics value and its staleness wrapper, without the channel
// that publishes them.  A reader that only holds a sample, such as a
// visualizer, includes this header and not observe/Metrics.h.
//
// The payload is fixed-size and trivially copyable so a snapshot can be
// published by value. A growable or span-backed payload would need heap
// ownership or leave the reader holding a borrow.

#include <fixy/Stale.h>

#include <array>
#include <cstdint>

namespace crucible::observe {

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

using RuntimeMetricsSample = ::fixy::Stale<RuntimeMetrics>;

[[nodiscard]] inline RuntimeMetricsSample fresh_metrics_sample(RuntimeMetrics metrics) noexcept {
    return RuntimeMetricsSample::fresh(metrics);
}

[[nodiscard]] inline RuntimeMetricsSample metrics_sample_at(RuntimeMetrics metrics, std::uint64_t staleness) noexcept {
    return RuntimeMetricsSample::at(metrics, staleness);
}

}  // namespace crucible::observe
