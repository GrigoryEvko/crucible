// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The sampling rate is in parts per million.  The door into
// SdcSamplingRatePpm must refuse 1,000,001, the value immediately above
// the closed range, so that no rate above 100% crosses the SdcConfig
// boundary.
//
// Expected diagnostic: the in_range precondition of mint_refined fails in
// a constant expression.

#include <crucible/observe/SdcDetect.h>

namespace observe = crucible::observe;

int main() {
    constexpr observe::SdcSamplingRatePpm bad_rate =
        ::fixy::mint_refined<observe::SdcSamplingRatePpm::predicate_type{}, observe::SdcSamplingRatePpm::value_type>(
            1000001u);
    (void)bad_rate;
    return 0;
}
