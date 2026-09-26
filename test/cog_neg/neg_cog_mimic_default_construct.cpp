// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A default CogMimic had no identity and held default caps under the
// Calibrated tag, a claim that no calibration made.  The default
// constructor is deleted, so every CogMimic is bound by the mint.

#include <crucible/mimic/CogMimic.h>

namespace cog = crucible::cog;
namespace mimic = crucible::mimic;

int main() {
    const mimic::CogMimic<cog::CogKind::Gpu> unbound{};
    (void)unbound;
    return 0;
}
