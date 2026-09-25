// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SPSC session mint fixture:
// safety::proto::spsc_session::mint_producer_session rejects a
// ConsumerHandle (wrong direction).
//
// Violation: `mint_producer_session<Channel>(ctx, handle)` takes
// `typename Channel::ProducerHandle&`.  Passing a ConsumerHandle
// fails the template argument deduction / type match at the call
// site.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at ProducerHandle vs ConsumerHandle.

#include <crucible/concurrent/_PermissionedSpscChannel.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SpscSession.h>

#include <utility>

namespace fspsc = ::crucible::safety::proto::spsc_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace neg_fixy_substr_spsc_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_spsc_wrong_handle

int main() {
    using Channel = conc::PermissionedSpscChannel<int, 64, neg_fixy_substr_spsc_wrong_handle::UserTag>;

    Channel ch{};
    auto whole = saf::mint_permission_root<typename Channel::whole_tag>();
    auto [prod_perm, cons_perm] =
        saf::mint_permission_split<typename Channel::producer_tag, typename Channel::consumer_tag>(std::move(whole));
    (void)prod_perm;
    auto cons_handle = ch.consumer(std::move(cons_perm));

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the ConsumerHandle to mint_producer_session — fails.
    [[maybe_unused]] auto bad = fspsc::mint_producer_session<Channel>(ctx, cons_handle);
    return 0;
}
