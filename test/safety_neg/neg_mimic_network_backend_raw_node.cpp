// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Planning takes an admitted node under the Ir001 source, and a raw comm
// descriptor is refused.  The CogMimic arrives as a parameter, because only
// its mint builds one.

#include <crucible/mimic/_wip/network/Backend.h>

namespace ir = crucible::forge::ir001;
namespace mb = crucible::mimic::_wip::network;

[[maybe_unused]] static void plan_raw(crucible::mimic::CogMimic<crucible::cog::CogKind::CpuSocket> const& mimic) {
    ir::AllReduceOp raw{};
    auto constraints = crucible::forge::recipes::query_constraints(crucible::NumericalRecipe{});
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Cpu>(mimic, raw, constraints);
    (void)planned;
}

int main() { return 0; }
