// Negative-compile test.  This file must fail to compile.
//
// The active view of the replay cursor is minted only from the foreground
// context of a Vigil's producer claim.  The context of another state's
// claim proves that its holder owns that state, not a Vigil.

#include <crucible/CrucibleContext.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto av = ctx.engine().mint_active_view(::foundation::effects::testing::foreground<Stranger>());
    (void)av;
    return 0;
}
