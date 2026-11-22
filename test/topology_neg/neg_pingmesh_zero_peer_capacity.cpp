// Negative-compile test. This file must fail to compile.
//
// An all-pairs pingmesh needs at least two peers.  A one-peer matrix has
// no source and destination pair to measure.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<::fixy::ColdInitCtx, 1>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    (void)mesh;
    return 0;
}
