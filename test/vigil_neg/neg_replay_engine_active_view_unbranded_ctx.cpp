// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The active view of the replay cursor is minted only from the foreground
// context of a Vigil's producer claim.  A context that names no claim
// states only that some thread holds some claim, so the gate refuses it.

#include <crucible/ReplayEngine.h>

int main() {
    crucible::ReplayEngine engine;
    auto av = engine.mint_active_view(::foundation::effects::testing::foreground());
    (void)av;
    return 0;
}
