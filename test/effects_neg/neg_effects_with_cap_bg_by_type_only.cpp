// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A foreground context, promoted to a background context by the name
// of the capability alone.  The promotion takes the new capability as
// an argument, so a caller must hold a Bg to get a Bg context.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

int main() {
    constexpr auto forged = eff::ExecCtx<>{}.with_cap<eff::Bg>();
    (void)forged;
    return 0;
}
