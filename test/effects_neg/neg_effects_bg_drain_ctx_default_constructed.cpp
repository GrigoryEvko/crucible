// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A background drain context, built with no key and no source context.
// Every ctx-bound gate that admits Bg admits the result, so the build
// must refuse it.  The default constructor of ExecCtx exists only for
// the foreground context.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

int main() {
    eff::BgDrainCtx forged{};
    (void)forged;
    return 0;
}
