// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #5 for GAPS-180. Sampling rate is parts-per-million, so
// values above 1,000,000 cannot cross the SdcConfig boundary.

#include <crucible/observe/SdcDetect.h>

namespace observe = crucible::observe;

constexpr observe::SdcSamplingRatePpm bad_rate{1'000'001};

int main() {
    (void)bad_rate;
    return 0;
}
