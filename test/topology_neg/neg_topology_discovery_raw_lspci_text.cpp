// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-111 fixture #2: lspci parser input is Tagged<source::External>.
// Raw strings cannot cross the discovery trust boundary implicitly.

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
    auto snapshot = crucible::topology::DefaultDiscoverySnapshot{};
    auto parsed = crucible::topology::parse_lspci_vmm_tree(
        "Slot:\t0000:00:00.0\n", snapshot);
    (void)parsed;
    return 0;
}
