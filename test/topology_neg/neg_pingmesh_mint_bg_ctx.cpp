// Negative-compile test. This file must fail to compile.
//
// Only an Init-row context mints a pingmesh.  A background worker records
// measurements into a pingmesh, but it cannot create one.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<::fixy::BgDrainCtx, 2>(
        ::fixy::BgDrainCtx{::foundation::effects::testing::bg()});
    (void)mesh;
    return 0;
}
