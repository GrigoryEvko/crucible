// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-072 fixture #3: a sender at generation 3 cannot weaken a reshard
// handoff by declaring MinGeneration 2. Sender-side EpochedDelegate
// minting requires an exact (epoch, generation) match so stale peers
// cannot satisfy a deliberately lowered threshold.

#include <crucible/sessions/SessionMint.h>

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    using CurrentCtx = proto::EpochExecCtx<6, 3, eff::HotFgCtx>;
    using Proto = proto::EpochedDelegate<
        proto::DelegatedSession<proto::End, proto::EmptyPermSet>,
        proto::End,
        6,
        2>;

    CurrentCtx ctx{};
    [[maybe_unused]] auto h = proto::mint_session<Proto>(ctx, FakeChannel{});
    return 0;
}
