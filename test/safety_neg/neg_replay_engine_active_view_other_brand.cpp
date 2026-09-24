// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The active view of the replay cursor is minted only from the foreground
// context of a Vigil's producer claim.  The context of another state's
// claim proves that its holder owns that state, not a Vigil.

#include <crucible/ReplayEngine.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::ReplayEngine engine;
    auto av = engine.mint_active_view(::foundation::effects::testing::foreground<Stranger>());
    (void)av;
    return 0;
}
