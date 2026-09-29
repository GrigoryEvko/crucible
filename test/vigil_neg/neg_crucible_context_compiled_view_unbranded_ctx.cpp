// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The compiled view of a context is minted only from the foreground
// context of a Vigil's producer claim.  A context that names no claim
// states only that some thread holds some claim, so the gate refuses it.

#include <crucible/CrucibleContext.h>

int main() {
    crucible::CrucibleContext ctx;
    auto cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground());
    (void)cv;
    return 0;
}
