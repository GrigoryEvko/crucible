// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body runs inside the fork's noexcept frame, so a body that may throw
// would terminate the program instead of reporting.  The body below can
// be called with the view of its child and the context, but it is not
// noexcept.  The inline arm refuses the call at its door, because
// can_each_body_take_its_child asks for a nothrow call.
//
// Without the nothrow conjunct the call passes the door and fails later,
// at the fork's own static assertion inside the body.  The regexes below
// name the door, so they tell the two apart.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

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

using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole), [](auto const& /*left_view*/, FgCtx const&) {},
        [](auto const& /*right_view*/, FgCtx const&) noexcept {});
    return 0;
}
