// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An n-ary combine rebuilds a parent from its children, so a manifest
// must say that the parent splits into exactly those children.  The
// children here come from a split of Whole, and the combine asks for
// Other.  Only the authoring witness is written for Other, as a forger
// who knew one of the two traits would write it.  The manifest check in
// the body of combine_n is what refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_combine_n
// that asks for can_split_into_pack<Parent, Children...>.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Other, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto [left, right] = perm::mint_permission_split_n<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine_n<Other>(std::move(left), std::move(right));
    return 0;
}
