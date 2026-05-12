// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-134. All-pairs pingmesh storage needs at least
// two peers; zero/one-peer matrices cannot represent source/destination
// latency pairs.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<
        crucible::effects::ColdInitCtx, 1>(crucible::effects::ColdInitCtx{});
    (void)mesh;
    return 0;
}
