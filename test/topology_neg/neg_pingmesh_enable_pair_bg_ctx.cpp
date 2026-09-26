// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// enable_pair writes the pair mask that record_measurement reads, so it
// takes an Init-row context.  A background worker cannot turn a pair on.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<::fixy::ColdInitCtx, 2>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    (void)mesh.enable_pair(::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, crucible::cog::Uuid{0x1, 0x1},
                           crucible::cog::Uuid{0x1, 0x2});
    return 0;
}
