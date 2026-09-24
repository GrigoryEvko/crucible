// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A class derived from Bg, built with empty braces.  The derived class
// is an aggregate, so the braces initialize the Bg base from nothing.
// The default constructor of Bg is private, and the derived class is not
// a friend, so the access check refuses the base.

#include <crucible/effects/_Capabilities.h>

namespace eff = ::crucible::effects;

struct DerivedFromBg : eff::Bg {};

int main() {
    DerivedFromBg derived{};
    const eff::Bg forged = derived;
    (void)forged;
    return 0;
}
