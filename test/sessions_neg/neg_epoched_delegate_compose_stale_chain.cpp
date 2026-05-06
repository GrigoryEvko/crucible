// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-072 fixture #4: composing two EpochedDelegate handoffs must not
// let the continuation chain lower its required epoch. The outer
// handoff is fresh at epoch 6; the composed continuation attempts to
// delegate again at epoch 5 and is rejected by the same mint gate.

#include <crucible/sessions/SessionMint.h>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    using CurrentCtx = proto::EpochExecCtx<6, 3, eff::HotFgCtx>;
    using Payload =
        proto::DelegatedSession<proto::End, proto::EmptyPermSet>;
    using Fresh = proto::EpochedDelegate<Payload, proto::End, 6, 3>;
    using StaleContinuation =
        proto::EpochedDelegate<Payload, proto::End, 5, 3>;
    using Composed = proto::compose_t<Fresh, StaleContinuation>;

    CurrentCtx ctx{};
    [[maybe_unused]] auto h = proto::mint_session<Composed>(ctx, FakeChannel{});
    return 0;
}
