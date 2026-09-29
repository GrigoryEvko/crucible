// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The compiled view of a context is minted only from the foreground
// context of a Vigil's producer claim.  The context of another state's
// claim proves that its holder owns that state, not a Vigil.

#include <crucible/CrucibleContext.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::CrucibleContext ctx;
    auto cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<Stranger>());
    (void)cv;
    return 0;
}
