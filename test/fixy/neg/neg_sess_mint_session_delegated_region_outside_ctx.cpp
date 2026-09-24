// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol receives a delegated endpoint whose permission set holds
// a region whose touch does IO.  The tags of that set move to the
// receiver with the endpoint.  The background context holds Bg and
// Alloc, and no IO, so mint_session rejects the call.  With a context
// that holds IO, the file compiles.
//
// Expected diagnostic: CtxAdmitsProtocolRow is not satisfied, at its
// clause for the delivered permission rows.

#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>

#include <utility>

namespace delegated_region_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Wire {};
struct IoRegion {
    using permission_row = eff::Row<eff::Effect::IO>;
};
using Endpoint = s::DelegatedSession<s::End, Wire, s::DefaultAbandonmentPolicy,
                                     ::foundation::permissions::PermSet<IoRegion>>;
using Proto = s::Recv<Endpoint, s::End>;
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
}  // namespace delegated_region_fixture

int main() {
    using namespace delegated_region_fixture;
    const BgCtx ctx{eff::testing::bg()};
    auto head = s::mint_session<Proto>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
