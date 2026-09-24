// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-147. SR-IOV enablement requires a
// source::SrIov-declared plan, not a raw SrIovPlan aggregate.

#include <crucible/cog/SrIov.h>

namespace sriov = crucible::cog::sriov;

int main() {
    sriov::SrIovPlan plan{};
    sriov::VfHandle handle{};
    auto result = sriov::enable(plan, std::span<sriov::VfHandle>{&handle, 1});
    return result.has_value() ? 0 : 1;
}
