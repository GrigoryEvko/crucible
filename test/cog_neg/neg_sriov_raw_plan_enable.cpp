// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Enabling virtual functions takes a declared plan. A raw plan never
// passed the mint or its validation, and it does not convert to one.

#include <crucible/cog/SrIov.h>

// The enable path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sriov = crucible::cog::sriov;

int main() {
    sriov::SrIovPlan plan{};
    sriov::VfHandle handle{};
    auto result = sriov::enable(plan, std::span<sriov::VfHandle>{&handle, 1});
    return result.has_value() ? 0 : 1;
}
