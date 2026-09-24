// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Every tag a split names needs an effect row, or nothing says which
// contexts may touch the region of that child.  The child tag here
// declares no row.  The token form of the split compares no row in its
// fit concept, so the row check in the body is what refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_split that
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
struct can_split_into<Whole, Left, RowlessRight> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Left, RowlessRight> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto halves = perm::mint_permission_split<Left, RowlessRight>(std::move(whole));
    return 0;
}
