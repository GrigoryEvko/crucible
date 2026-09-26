// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-072 fixture #2: a recipient still at epoch 4 cannot accept a
// reshard handoff that requires epoch 5. EpochedAccept admits newer
// recipients, but stale recipients fail at the ctx-bound mint.

#include <crucible/sessions/_SessionMint.h>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}  // namespace

int main() {
    using StaleRecipientCtx = proto::EpochExecCtx<4, 3, eff::HotFgCtx>;
    using Proto = proto::EpochedAccept<proto::DelegatedSession<proto::End, proto::EmptyPermSet>, proto::End, 5, 3>;

    proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
    const StaleRecipientCtx ctx = proto::with_session_epoch<4, 3>(eff::HotFgCtx{}, source);
    [[maybe_unused]] auto h = proto::mint_permissioned_session<Proto>(ctx, FakeChannel{});
    return 0;
}
