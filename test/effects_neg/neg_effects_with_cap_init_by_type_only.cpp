// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The hot foreground context, promoted to a startup context by the
// name of the capability alone.  The promotion takes the new capability
// as an argument, so a caller must hold an Init to get an Init context.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

int main() {
    constexpr auto forged = eff::HotFgCtx{}.with_cap<eff::Init>();
    (void)forged;
    return 0;
}
