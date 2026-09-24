// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Every tag an n-ary combine names needs an effect row.  The parent tag
// of the combine here declares none, so nothing says which contexts may
// own the region it rebuilds.  The fit concept of the token form
// compares no row, so the row check in the body of combine_n refuses the
// call.
//
// Expected diagnostic: the static_assert in mint_permission_combine_n
// that asks for a row on every tag the combine names.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct RowlessWhole {};
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
struct can_split_into_pack<RowlessWhole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<RowlessWhole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto [left, right] = perm::mint_permission_split_n<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine_n<RowlessWhole>(std::move(left), std::move(right));
    return 0;
}
