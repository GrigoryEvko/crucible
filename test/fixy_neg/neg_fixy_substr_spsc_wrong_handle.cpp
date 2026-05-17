// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Substr fixture #1: mint_producer_session via fixy::
// alias rejects a ConsumerHandle (wrong direction).
//
// Violation: `mint_producer_session<Channel>(ctx, handle)` takes
// `typename Channel::ProducerHandle&`.  Passing a ConsumerHandle
// fails the template argument deduction / type match at the call
// site.  Routing through `fixy::substr::spsc::mint_producer_session`
// must reject identically.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at ProducerHandle vs ConsumerHandle.

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

#include <utility>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace eff     = crucible::effects;
namespace saf     = crucible::safety;

namespace neg_fixy_substr_spsc_wrong_handle {
struct UserTag {};
}

int main() {
    using Channel =
        conc::PermissionedSpscChannel<int, 64, neg_fixy_substr_spsc_wrong_handle::UserTag>;

    Channel ch{};
    auto whole = saf::mint_permission_root<typename Channel::whole_tag>();
    auto [prod_perm, cons_perm] = saf::mint_permission_split<
        typename Channel::producer_tag,
        typename Channel::consumer_tag>(std::move(whole));
    (void)prod_perm;
    auto cons_handle = ch.consumer(std::move(cons_perm));

    eff::BgCompileCtx ctx{};
    // Pass the ConsumerHandle to mint_producer_session — fails.
    [[maybe_unused]] auto bad =
        fsubstr::spsc::mint_producer_session<Channel>(ctx, cons_handle);
    return 0;
}
