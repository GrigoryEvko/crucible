// mint_spawn_on_host runs a spawn on the facts of a host that its caller
// gives, so its second context must own Test.  Code that ships cannot then
// give a spawn the caches of a host of its own.

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
    BgCtx not_a_test{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt =
        fixy::spawn::mint_spawn_on_host<fixy::spawn::BodyCoupling::WaitOnEachOther, Left, Right>(
            ctx, not_a_test, fixy::concurrent::HostFacts{}, fixy::concurrent::WorkBudget{}, std::move(whole),
            [](auto const& /*left_view*/, BgCtx const&) noexcept {},
            [](auto const& /*right_view*/, BgCtx const&) noexcept {});
    return 0;
}
