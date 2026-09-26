// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The scorer's constructor is private: mint_topology_health, gated on an
// Init-row context, is the only way to build one.

#include <crucible/topology/Health.h>

int main() {
    crucible::topology::CompositeHealthScorer<2, 32, 8> scorer{crucible::topology::HealthPolicy{}};
    (void)scorer;
    return 0;
}
