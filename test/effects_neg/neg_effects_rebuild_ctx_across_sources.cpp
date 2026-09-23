// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The hot foreground context, rebuilt as a background context.  A
// rebuild keeps the capability of the old context, or drops to the
// foreground context.  It never raises Fg to Bg, because the foreground
// context holds no capability to carry across.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

int main() {
    const eff::HotFgCtx foreground{};
    auto forged = eff::rebuild_ctx_to<eff::BgDrainCtx>(foreground);
    (void)forged;
    return 0;
}
