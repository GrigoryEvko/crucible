// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// mint_sdc_detector is the only door into a detector, and its gate
// admits only an Init-row context.  A detector built directly from a
// config would skip that gate, so the constructor is private.

#include <crucible/observe/SdcDetect.h>

namespace observe = crucible::observe;

int main() {
    observe::SdcDetector<2, 4> detector{observe::SdcConfig{}};
    (void)detector;
    return 0;
}
