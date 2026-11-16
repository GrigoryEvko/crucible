// mint_spawn_on_host can start one thread for each child, so its first
// context must own Bg, as the context of mint_spawn must.  A context that
// owns only Test is refused.

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
using TestCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    TestCtx ctx{eff::testing::test()};
    TestCtx test_ctx{eff::testing::test()};
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt =
        fixy::spawn::mint_spawn_on_host<fixy::spawn::BodyCoupling::WaitOnEachOther, Left, Right>(
            ctx, test_ctx, fixy::concurrent::HostFacts{}, fixy::concurrent::WorkBudget{}, std::move(whole),
            [](auto const& /*left_view*/, TestCtx const&) noexcept {},
            [](auto const& /*right_view*/, TestCtx const&) noexcept {});
    return 0;
}
