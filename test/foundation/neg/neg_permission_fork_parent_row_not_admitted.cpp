// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fork splits the parent under the caller's context, so the context
// must admit the parent's row, and not only the rows of the children.
// Here the children are pure and the parent does IO, and the foreground
// context admits no IO.  The fit concept of the inline arm refuses the
// call at its door and names the parent's check.
//
// Without that conjunct the call passes the door and fails later, inside
// the body, at the split.  That failure names no fork, so the regexes
// below tell the two apart.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};

using TestCtx = ::foundation::effects::detail::ctx_witnesses::TestWitnessCtx;
using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>(TestCtx{::foundation::effects::testing::test()});
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [](::foundation::permissions::Permission<Left>, FgCtx const&) noexcept {},
        [](::foundation::permissions::Permission<Right>, FgCtx const&) noexcept {});
    return 0;
}
