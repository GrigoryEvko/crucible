// A session that starts with a permission holds the region of its tag,
// and a tag can carry an effect row.  Here the region is on disk, so its
// use does IO.  The permissioned mint asks that the context admit the row
// of each tag.  A background drain context has the rows Bg and Alloc, and
// no IO, so the gate refuses it.  The protocol, the Resource and the
// token are all correct, so only the row clause can refuse the call.

#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

namespace row_refused_fixture {
struct DiskRegion {
    using permission_row = eff::Row<eff::Effect::IO>;
};
struct Wire {};
using TestCtx = eff::detail::ctx_witnesses::TestRunnerCtx;
// The one root site of DiskRegion, so the protocol names the brand of its
// root.
[[nodiscard]] inline auto mint_disk_root(TestCtx const& ctx) noexcept {
    return perm::mint_permission_root<DiskRegion>(ctx);
}
using DiskToken = decltype(mint_disk_root(std::declval<TestCtx const&>()));
using SendsRegion = s::Send<DiskToken, s::End>;
}  // namespace row_refused_fixture

int main() {
    using namespace row_refused_fixture;
    const TestCtx test_ctx{eff::testing::test()};
    const eff::detail::ctx_witnesses::BgWitness drain_ctx{eff::testing::bg()};
    auto head = s::mint_permissioned_session<SendsRegion>(drain_ctx, Wire{}, mint_disk_root(test_ctx));
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
