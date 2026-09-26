// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A calibrated throughput is a finite positive rate. Zero is a probe
// that measured nothing, so the positive conjunct refuses it at the
// checked door.

#include <crucible/cog/Calibrate.h>

namespace cog = crucible::cog;

constexpr cog::CalibratedThroughput bad_throughput = ::fixy::mint_refined<cog::finite_positive_throughput>(0.0);

int main() { return static_cast<int>(bad_throughput.value()); }
