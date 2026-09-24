// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A split hands out one token per child, so two children of one tag
// would be two owners of one region.  The manifest here declares the
// parent split into the same tag twice, with its witness, and the
// distinctness check in the body of the split refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_split that
// asks for two distinct child tags.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Half {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into<Whole, Half, Half> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Half, Half> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto halves = perm::mint_permission_split<Half, Half>(std::move(whole));
    return 0;
}
