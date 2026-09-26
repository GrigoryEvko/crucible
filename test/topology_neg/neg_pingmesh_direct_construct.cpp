// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The pingmesh constructor is private: mint_pingmesh, gated on an
// Init-row context, is the only way to build one.

#include <crucible/topology/Pingmesh.h>

int main() {
    crucible::topology::Pingmesh<2> mesh{crucible::topology::PingmeshConfig{}};
    (void)mesh;
    return 0;
}
