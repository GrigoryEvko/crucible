// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-072 fixture #1: a sender at epoch 4 cannot mint a reshard
// handoff whose protocol declares MinEpoch 5. The ctx-bound mint must
// reject this before any delegated endpoint is constructed.

#include <crucible/sessions/SessionMint.h>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    using StaleSenderCtx = proto::EpochExecCtx<4, 3, eff::HotFgCtx>;
    using Proto = proto::EpochedDelegate<
        proto::DelegatedSession<proto::End, proto::EmptyPermSet>,
        proto::End,
        5,
        3>;

    StaleSenderCtx ctx{};
    [[maybe_unused]] auto h = proto::mint_session<Proto>(ctx, FakeChannel{});
    return 0;
}
