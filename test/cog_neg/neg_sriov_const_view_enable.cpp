// Negative compile test. This file must fail to compile.
//
// enable writes the handles into a run that its caller lends as the one
// writer. A View of const handles is no such run, and no handle list gives
// a View that writes.

#include <crucible/cog/SrIov.h>

#include <foundation/core/List.h>

// The enable path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sriov = crucible::cog::sriov;

int main() {
    sriov::DeclaredSrIovPlan plan = ::fixy::mint_tagged<::fixy::tags::source::SrIov>(sriov::SrIovPlan{});
    ::foundation::core::FixedList<sriov::VfHandle, 1> handles{};
    auto result = sriov::enable(plan, handles.view());
    return result.is_ok() ? 0 : 1;
}
