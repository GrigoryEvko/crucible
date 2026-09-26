// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fork lends each body a view under the brand of the parent, and the
// body gate asks for exactly that view.  A body that asks for the view of
// another region of the same tag is refused at the door, so the gate and
// the call agree on the type that the body gets.
//
// Two regions of one tag are minted at two sites, so they are two brands.
// The body below spells the brand of the region that is not forked.

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
    namespace fp = ::foundation::permissions;
    auto forked = fp::mint_permission_root<Whole>();
    auto other = fp::mint_permission_root<Whole>();
    using OtherBrand = ::foundation::brand::brand_of_t<decltype(other)>;
    [[maybe_unused]] auto rebuilt = fp::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(forked),
        [](fp::WriteView<Left, OtherBrand> const&, FgCtx const&) noexcept {},
        [](auto const& /*right_view*/, FgCtx const&) noexcept {});
    fp::permission_drop(std::move(other));
    return 0;
}
