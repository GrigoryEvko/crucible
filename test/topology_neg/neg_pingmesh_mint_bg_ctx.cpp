// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-134. Pingmesh carriers are minted only by Init
// contexts; background workers may publish measurements but not create
// the all-pairs substrate.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<
        crucible::effects::BgDrainCtx, 2>(crucible::effects::BgDrainCtx{});
    (void)mesh;
    return 0;
}
