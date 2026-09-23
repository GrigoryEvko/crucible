// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// An aggregate that holds a background drain context, built with empty
// braces.  The braces initialize the member from nothing, and a copy of
// the member then leaves the holder.  The member takes a default
// constructor only over the foreground context, so the braces fail.

#include <crucible/effects/_ExecCtx.h>

namespace eff = ::crucible::effects;

struct HolderOfBgDrain {
    eff::BgDrainCtx held;
};

int main() {
    HolderOfBgDrain holder{};
    const eff::BgDrainCtx forged = holder.held;
    (void)forged;
    return 0;
}
