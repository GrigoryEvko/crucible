// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An n-ary combine needs the pack manifest and its authoring witness.
// The manifest for Other here is written alone, with no witness beside
// it, and the witness check in the body of combine_n refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_combine_n
// that asks for has_split_pack_authoring_witness.

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
struct can_split_into_pack<Other, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto [left, right] = perm::mint_permission_split_n<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine_n<Other>(std::move(left), std::move(right));
    return 0;
}
