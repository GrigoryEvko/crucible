// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A startup context, built with no key and no source context.  Every
// ctx-bound gate that admits Init admits the result, so the build must
// refuse it.  The default constructor of ExecCtx exists only for the
// foreground context.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

int main() {
    eff::ColdInitCtx forged{};
    (void)forged;
    return 0;
}
