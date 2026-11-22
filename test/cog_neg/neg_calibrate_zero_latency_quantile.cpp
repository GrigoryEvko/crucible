// Negative compile test. This file must fail to compile.
//
// A calibrated measurement row carries ordered latency quantiles with a
// positive median. The zero default sentinel is not a measured sample,
// so the median conjunct of the calibration predicate refuses it at the
// checked door.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;

constexpr cog::CalibrationLatencyQuantiles bad_latency =
    ::fixy::mint_refined<cog::calibration_quantiles_valid>(cog::LatencyQuantiles{0u, 1u, 2u});

int main() { return static_cast<int>(bad_latency.value().p50_ns); }
