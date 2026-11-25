// Negative compile test. This file must fail to compile.
//
// Enabling virtual functions takes a declared plan. A raw plan never
// passed the mint or its validation, and it does not convert to one.

#include <crucible/cog/SrIov.h>

// The enable path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sriov = crucible::cog::sriov;

using HandleRun = ::fixy::ViewMut<sriov::VfHandle, ::foundation::brand::DefaultBrand>;

namespace {

[[maybe_unused]] bool enable_raw_plan(sriov::SrIovPlan plan, HandleRun& run) {
    return sriov::enable(plan, run).is_ok();
}

}  // namespace

int main() { return 0; }
