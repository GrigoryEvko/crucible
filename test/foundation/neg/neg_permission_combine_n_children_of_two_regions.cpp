// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Two regions of one tag split into the same children tags, and each
// child carries the brand of its own region.  An n-ary combine of one
// child from each region would rebuild a parent that neither region
// is.  The fit concept of combine_n reads no brand, so the brand check
// in its body is what refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_combine_n
// that asks the children to carry one brand.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
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
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto first = perm::mint_permission_root<Whole>();
    auto second = perm::mint_permission_root<Whole>();
    auto [first_left, first_right] = perm::mint_permission_split_n<Left, Right>(std::move(first));
    auto [second_left, second_right] = perm::mint_permission_split_n<Left, Right>(std::move(second));
    [[maybe_unused]] auto mixed =
        perm::mint_permission_combine_n<Whole>(std::move(first_left), std::move(second_right));
    return 0;
}
