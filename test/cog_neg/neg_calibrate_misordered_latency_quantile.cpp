// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A positive median does not rescue a triple whose quantiles are out of
// order. The ordering conjunct of the calibration predicate refuses it at
// the checked door, so the conjunction holds both halves.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;

constexpr cog::CalibrationLatencyQuantiles bad_latency =
    ::fixy::mint_refined<cog::calibration_quantiles_valid>(cog::LatencyQuantiles{90u, 50u, 70u});

int main() { return static_cast<int>(bad_latency.value().p50_ns); }
