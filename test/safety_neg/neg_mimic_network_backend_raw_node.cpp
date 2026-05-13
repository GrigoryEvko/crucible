// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/mimic/_wip/network/Backend.h>

namespace ir = crucible::forge::ir001;
namespace mb = crucible::mimic::_wip::network;

int main() {
    crucible::mimic::CogMimic<crucible::cog::CogKind::CpuSocket> mimic{};
    ir::AllReduceOp raw{};
    auto constraints = crucible::forge::recipes::query_constraints(
        crucible::NumericalRecipe{});
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Cpu>(
        mimic, raw, constraints);
    (void)planned;
}
