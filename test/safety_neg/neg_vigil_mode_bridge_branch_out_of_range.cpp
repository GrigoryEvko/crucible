// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-080: Vigil's mode bridge exposes exactly two live transition
// branches plus End.  A fourth branch would be an unmodelled mode
// transition and must fail at the session-protocol boundary.

#include <crucible/Vigil.h>

#include <utility>

int main() {
    crucible::Vigil vigil{};
    auto session = crucible::mint_vigil_mode_bridge(vigil);
    [[maybe_unused]] auto impossible = std::move(session).select_local<3>();
    return 0;
}
