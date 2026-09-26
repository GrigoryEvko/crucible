// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The view that the fork lends a body is the proof of its child, and it
// ends with the body.  A body that could copy or move its view into
// storage outside its frame would keep a proof past the join.  The view
// has no copy and no move.
//
// The body below takes the view of its child and copies it into a static
// local, which outlives the call.

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
    auto whole = fp::mint_permission_root<Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    [[maybe_unused]] auto rebuilt = fp::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [](fp::WriteView<Left, WholeBrand> const& left, FgCtx const&) noexcept {
            static fp::WriteView<Left, WholeBrand> const kept{left};
            (void)kept;
        },
        [](auto const& /*right_view*/, FgCtx const&) noexcept {});
    return 0;
}
