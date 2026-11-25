// Negative-compile test.  This file must fail to compile.
//
// The active view of the replay cursor is minted only from the foreground
// context of a Vigil's producer claim.  A context that names no claim
// states only that some thread holds some claim, so the gate refuses it.

#include <crucible/CrucibleContext.h>

int main() {
    crucible::CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto av = ctx.engine().mint_active_view(::foundation::effects::testing::foreground());
    (void)av;
    return 0;
}
