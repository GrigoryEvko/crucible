// A session that starts with a permission holds the region of its tag,
// and a tag can carry an effect row.  Here the region is on disk, so its
// use does IO.  The permissioned mint asks that the context admit the row
// of each tag.  A background drain context has the rows Bg and Alloc, and
// no IO, so the gate refuses it.  The protocol, the Resource and the
// token are all correct, so only the row clause can refuse the call.

#include <fixy/session/Handle.h>

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
using SendsRegion = s::Send<perm::Permission<DiskRegion>, s::End>;
}  // namespace row_refused_fixture

int main() {
    using namespace row_refused_fixture;
    const eff::detail::ctx_witnesses::TestRunnerCtx test_ctx{eff::testing::test()};
    const eff::detail::ctx_witnesses::BgWitness drain_ctx{eff::testing::bg()};
    auto [head, hold] = s::mint_permissioned_session<SendsRegion>(drain_ctx, Wire{},
                                                                  perm::mint_permission_root<DiskRegion>(test_ctx));
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    static_cast<void>(std::move(hold).into_permissions());
    return 0;
}
