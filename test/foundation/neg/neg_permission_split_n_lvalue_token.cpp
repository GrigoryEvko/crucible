// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// split_n consumes its parent and mints one token per child.  A parent
// passed as an lvalue would stay live beside its children, so the one
// region would have two owners.  The argument-shape half of the fit
// concept refuses the lvalue.  The row half alone admits it, because
// the call names no context.
//
// Expected diagnostic: the split_n fit concept is not satisfied, and the
// shape check answers false.

#include <foundation/permissions/Permission.h>

#include <type_traits>

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
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto parts = ::foundation::permissions::mint_permission_split_n<Left, Right>(whole);
    return 0;
}
