// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Every tag an n-ary split names needs an effect row.  The second child
// here declares none.  The fit concept of the token form compares no
// row, so the row check in the body of split_n refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_split_n that
// asks for a row on every tag the split names.

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
struct RowlessRight {};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, RowlessRight> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, RowlessRight> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto parts = perm::mint_permission_split_n<Left, RowlessRight>(std::move(whole));
    return 0;
}
