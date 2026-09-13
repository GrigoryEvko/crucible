#pragma once

// The keeper reader mint and the canopy reader mint differ in name
// only. Both resolve to the same reader factory over the same channel.

#include <crucible/observe/Metrics.h>

#include <type_traits>

namespace crucible::fixy::observe {

using ::crucible::observe::mint_metrics_writer;
using ::crucible::observe::mint_keeper_metrics_reader;
using ::crucible::observe::mint_canopy_metrics_reader;

using ::crucible::observe::RuntimeMetrics;
using ::crucible::observe::RuntimeMetricsSample;
using ::crucible::observe::RuntimeMetricsComputation;

using ::crucible::observe::RuntimeMetricsChannel;
using ::crucible::observe::RuntimeMetricsWriter;
using ::crucible::observe::RuntimeMetricsReader;

using ::crucible::observe::RuntimeMetricsWriterTag;
using ::crucible::observe::RuntimeMetricsReaderTag;

using ::crucible::observe::fresh_metrics_sample;
using ::crucible::observe::metrics_sample_at;

}  // namespace crucible::fixy::observe

namespace crucible::fixy::observe::self_test {

static_assert(std::is_same_v<decltype(&::crucible::fixy::observe::mint_metrics_writer),
                             decltype(&::crucible::observe::mint_metrics_writer)>,
              "mint_metrics_writer must alias the substrate factory.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::observe::mint_keeper_metrics_reader),
                             decltype(&::crucible::observe::mint_keeper_metrics_reader)>,
              "mint_keeper_metrics_reader must alias the substrate factory.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::observe::mint_canopy_metrics_reader),
                             decltype(&::crucible::observe::mint_canopy_metrics_reader)>,
              "mint_canopy_metrics_reader must alias the substrate factory.");

static_assert(std::is_same_v<::crucible::fixy::observe::RuntimeMetrics, ::crucible::observe::RuntimeMetrics>,
              "RuntimeMetrics must alias the substrate type.");

static_assert(
    std::is_same_v<::crucible::fixy::observe::RuntimeMetricsSample, ::crucible::observe::RuntimeMetricsSample>,
    "RuntimeMetricsSample must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::observe::RuntimeMetricsComputation,
                             ::crucible::observe::RuntimeMetricsComputation>,
              "RuntimeMetricsComputation must alias the substrate type.");

static_assert(
    std::is_same_v<::crucible::fixy::observe::RuntimeMetricsChannel, ::crucible::observe::RuntimeMetricsChannel>,
    "RuntimeMetricsChannel must alias the substrate type.");

static_assert(
    std::is_same_v<::crucible::fixy::observe::RuntimeMetricsWriter, ::crucible::observe::RuntimeMetricsWriter>,
    "RuntimeMetricsWriter must alias the substrate handle.");

static_assert(
    std::is_same_v<::crucible::fixy::observe::RuntimeMetricsReader, ::crucible::observe::RuntimeMetricsReader>,
    "RuntimeMetricsReader must alias the substrate handle.");

static_assert(
    !std::is_same_v<::crucible::fixy::observe::RuntimeMetricsSample, ::crucible::fixy::observe::RuntimeMetrics>,
    "RuntimeMetricsSample must stay a staleness-wrapped payload. Collapsing "
    "it to the bare payload makes every sample read as fresh.");

}  // namespace crucible::fixy::observe::self_test
