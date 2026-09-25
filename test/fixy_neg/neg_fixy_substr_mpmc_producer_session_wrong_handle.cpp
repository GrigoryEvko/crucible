// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MPMC session mint fixture for
// safety::proto::mpmc_channel_session::mint_mpmc_producer_session:
// rejects a ConsumerHandle (wrong role).  mint_mpmc_producer_session
// takes `typename Channel::ProducerHandle&` (sessions/_MpmcChannelSession.h);
// passing a ConsumerHandle fails type match at the call site.
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_producer_session_non_ctx.cpp (role swap vs
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

namespace neg_fixy_substr_mpmc_producer_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_mpmc_producer_session_wrong_handle

int main() {
    conc::PermissionedMpmcChannel<int, 16, neg_fixy_substr_mpmc_producer_session_wrong_handle::UserTag> ch;

    auto consumer_opt = ch.consumer();

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the ConsumerHandle to the producer-session mint — expects
    // Channel::ProducerHandle&.
    [[maybe_unused]] auto bad = fmpmc::mint_mpmc_producer_session<decltype(ch)>(ctx, *consumer_opt);
    return 0;
}
