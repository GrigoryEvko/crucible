// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Every tag a combine names needs an effect row.  The parent tag of the
// combine here declares no row, so nothing says which contexts may own
// the region the combine rebuilds.  The token form of the combine
// compares no row in its fit concept, so the row check in the body is
// what refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_combine that
// asks for a row on every tag the combine names.

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
struct can_split_into<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Left, Right> : std::true_type {};
template <>
struct can_split_into<RowlessWhole, Left, Right> : std::true_type {};
template <>
struct has_split_authoring_witness<RowlessWhole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto halves = perm::mint_permission_split<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt =
        perm::mint_permission_combine<RowlessWhole>(std::move(halves.first), std::move(halves.second));
    return 0;
}
