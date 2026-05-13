// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-196. A calibrated measurement row must carry
// positive ordered latency quantiles; the zero default sentinel is not a
// valid measured sample.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;

constexpr cog::CalibrationLatencyQuantiles bad_latency{
    cog::LatencyQuantiles{0u, 1u, 2u}};

int main() {
    return static_cast<int>(bad_latency.value().p50_ns);
}
