#pragma once

// The runtime metrics value and its staleness wrapper, without the channel
// that publishes them.  A reader that only holds a sample, such as a
// visualizer, includes this header and not observe/Metrics.h.
//
// The payload is fixed-size and trivially copyable so a snapshot can be
// published by value. A growable or span-backed payload would need heap
// ownership or leave the reader holding a borrow.

#include <fixy/Stale.h>
#include <foundation/core/Array.h>

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
    ::foundation::core::FixedArray<double, 16> delta_g{};
};

// The grade of a sample counts the steps of the producer since the sample.
// The producer owns the source of its steps, and only that source grades a
// sample.
using RuntimeMetricsSample = ::fixy::Stale<RuntimeMetrics>;

// A sample of the current step of the source.
[[nodiscard]] inline RuntimeMetricsSample fresh_metrics_sample(RuntimeMetrics metrics,
                                                               ::fixy::SequenceSource const& steps) noexcept {
    return RuntimeMetricsSample::fresh(metrics, steps);
}

// A sample of the step of the stamp, graded at the current step of the source.
[[nodiscard]] inline RuntimeMetricsSample metrics_sample_at(RuntimeMetrics metrics, ::fixy::SequenceSource const& steps,
                                                            ::fixy::SequenceStamp const& sampled) noexcept {
    return RuntimeMetricsSample::at(metrics, steps, sampled);
}

}  // namespace crucible::observe
