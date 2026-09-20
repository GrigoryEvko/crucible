// mint_spawn forwards to the spawning arm of the fork, which starts one
// thread per child.  That is background work, so the context has to own
// Effect::Bg.  A foreground context is refused here rather than one layer
// down inside foundation.

#include <fixy/os/Spawn.h>
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
using FgCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    FgCtx ctx{eff::testing::test()};
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = fixy::spawn::mint_spawn<Left, Right>(
        ctx, std::move(whole), [](perm::Permission<Left>, FgCtx const&) noexcept {},
        [](perm::Permission<Right>, FgCtx const&) noexcept {});
    return 0;
}
