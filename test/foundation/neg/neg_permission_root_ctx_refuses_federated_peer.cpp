// A context whose row admits the IO row of a federation peer tag does
// not authorize a peer either.  Only a verified handshake does, so the
// root mint refuses the tag with a context too.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace eff = ::foundation::effects;

namespace {
struct PeerOrg {};
using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;
}  // namespace

int main() {
    IoCtx ctx{eff::testing::bg()};
    [[maybe_unused]] auto token =
        ::foundation::permissions::mint_permission_root<::foundation::permissions::tag::FederatedPeer<PeerOrg>>(ctx);
    return 0;
}
