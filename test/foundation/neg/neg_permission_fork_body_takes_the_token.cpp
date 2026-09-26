// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body that holds its child token can move the token out of the fork,
// into storage that outlives the join.  The parent that the fork gives
// back would then exist beside a live child.  So the fork lends each body
// a view of its child and keeps the token, and a body that asks for the
// token is refused at the door.
//
// The body below asks for the token of its child and stashes it.  The
// refusal is the body gate of the inline arm.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <optional>
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
    namespace fp = ::foundation::permissions;
    auto whole = fp::mint_permission_root<Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    std::optional<fp::Permission<Left, WholeBrand>> stash;
    [[maybe_unused]] auto rebuilt = fp::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [&stash](fp::Permission<Left, WholeBrand>&& left, FgCtx const&) noexcept { stash.emplace(std::move(left)); },
        [](auto const& /*right_view*/, FgCtx const&) noexcept {});
    return 0;
}
