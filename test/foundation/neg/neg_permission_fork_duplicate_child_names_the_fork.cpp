// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A manifest that lists one child tag twice would hand two bodies a token
// for the same region.  The pack trait and its witness below say exactly
// that, so the fork's door admits the call, and the fork's own check
// refuses it inside the body.  Its message names the arm, so a reader
// sees the fork and not only the split under it.
//
// The split also refuses duplicate tags.  Without the fork's own check
// only the split's message would remain, which names no fork, so the
// regexes below tell the two apart.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Half {
    using permission_row = ::foundation::effects::Row<>;
};

using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Half, Half> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Half, Half> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork_inline<Half, Half>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [](auto const& /*first_view*/, FgCtx const&) noexcept {},
        [](auto const& /*second_view*/, FgCtx const&) noexcept {});
    return 0;
}
