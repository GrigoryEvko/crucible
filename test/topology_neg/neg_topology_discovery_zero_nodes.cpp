// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-111 fixture #3: bounded DiscoverySnapshot storage must have at least
// one node slot and one edge slot. Zero-node snapshots are unusable.

#include <crucible/topology/Discovery.h>

int main() {
    using Ctx = crucible::effects::ExecCtx<
        crucible::effects::Init,
        crucible::effects::ctx_numa::Any,
        crucible::effects::ctx_alloc::Unbound,
        crucible::effects::ctx_heat::Cold,
        crucible::effects::ctx_resid::DRAM,
        crucible::effects::Row<crucible::effects::Effect::Init>,
        crucible::effects::ctx_workload::Unspecified>;
    Ctx ctx{};
    auto snapshot = crucible::topology::mint_discovery_snapshot<0, 1>(ctx);
    (void)snapshot;
    return 0;
}
