// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// combine_n consumes the children and rebuilds their parent.  A child
// passed as an lvalue would stay live beside the rebuilt parent, so the
// one region would have two owners.  The argument-shape half of the fit
// concept refuses the lvalue.  The row half alone admits it, because the
// call names no context.
//
// Expected diagnostic: the combine_n fit concept is not satisfied, and
// the shape check answers false.

#include <foundation/permissions/Permission.h>

#include <tuple>
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
    auto parts = perm::mint_permission_split_n<Left, Right>(perm::mint_permission_root<Whole>());
    auto& left = std::get<0>(parts);
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine_n<Whole>(left, std::move(std::get<1>(parts)));
    return 0;
}
