// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074g fixture for fixy::substr::mpmc::mint_mpmc_consumer_session:
// rejects a ProducerHandle (wrong role).  mint_mpmc_consumer_session
// takes `typename Channel::ConsumerHandle&` (MpmcChannelSession.h:281);
// passing a ProducerHandle fails type match — the role-inverse of the
// producer-session fixture.
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_consumer_session_non_ctx.cpp (role swap vs
// non-ExecCtx).
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at ProducerHandle vs ConsumerHandle.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace eff     = crucible::effects;

namespace neg_fixy_substr_mpmc_consumer_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_mpmc_consumer_session_wrong_handle

int main() {
    conc::PermissionedMpmcChannel<int, 16,
        neg_fixy_substr_mpmc_consumer_session_wrong_handle::UserTag> ch;

    auto producer_opt = ch.producer();

    eff::BgCompileCtx ctx{};
    // Pass the ProducerHandle to the consumer-session mint — expects
    // Channel::ConsumerHandle&.
    [[maybe_unused]] auto bad =
        fsubstr::mpmc::mint_mpmc_consumer_session<decltype(ch)>(
            ctx, *producer_opt);
    return 0;
}
