// mint_spawn takes the coupling of its bodies as its first template
// argument, with no default.  The arm of a spawn changes with the budget and
// with the caches of the host, and two bodies that wait on each other never
// end on the inline arm.  So a spawn that does not state whether its bodies
// wait on each other is refused.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

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
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    BgCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = fixy::spawn::mint_spawn<Left, Right>(
        ctx, fixy::concurrent::WorkBudget{}, std::move(whole), [](auto const& /*left_view*/, BgCtx const&) noexcept {},
        [](auto const& /*right_view*/, BgCtx const&) noexcept {});
    return 0;
}
