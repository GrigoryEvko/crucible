// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MPMC session mint fixture for
// safety::proto::mpmc_channel_session::mint_mpmc_consumer_session:
// rejects a ProducerHandle (wrong role).  mint_mpmc_consumer_session
// takes `typename Channel::ConsumerHandle&` (sessions/_MpmcChannelSession.h);
// passing a ProducerHandle fails type match — the role-inverse of the
// producer-session fixture.
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_consumer_session_non_ctx.cpp (role swap vs
// non-ExecCtx).
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at ProducerHandle vs ConsumerHandle.

#include <crucible/concurrent/_PermissionedMpmcChannel.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_MpmcChannelSession.h>

namespace fmpmc = ::crucible::safety::proto::mpmc_channel_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

namespace neg_fixy_substr_mpmc_consumer_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_mpmc_consumer_session_wrong_handle

int main() {
    conc::PermissionedMpmcChannel<int, 16, neg_fixy_substr_mpmc_consumer_session_wrong_handle::UserTag> ch;

    auto producer_opt = ch.producer();

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the ProducerHandle to the consumer-session mint — expects
    // Channel::ConsumerHandle&.
    [[maybe_unused]] auto bad = fmpmc::mint_mpmc_consumer_session<decltype(ch)>(ctx, *producer_opt);
    return 0;
}
