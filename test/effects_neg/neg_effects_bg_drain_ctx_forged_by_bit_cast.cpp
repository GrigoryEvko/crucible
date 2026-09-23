// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A background drain context, built from one byte.  The context holds a
// Bg, and cap() hands it out.  The Bg is not trivially copyable, so the
// context is not either, and std::bit_cast has no candidate.

#include <crucible/effects/_ExecCtx.h>

#include <bit>

namespace eff = ::crucible::effects;

int main() {
    auto forged = std::bit_cast<eff::BgDrainCtx>(char{0});
    (void)forged;
    return 0;
}
