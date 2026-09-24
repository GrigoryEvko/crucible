// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A background capability, built from one byte.  Each constructor of
// Bg is user-provided, so the type is not trivially copyable, and
// std::bit_cast has no candidate.

#include <crucible/effects/_Capabilities.h>

#include <bit>

namespace eff = ::crucible::effects;

int main() {
    auto forged = std::bit_cast<eff::Bg>(char{0});
    (void)forged;
    return 0;
}
