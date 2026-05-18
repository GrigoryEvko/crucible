// fixy_neg: mint_sender refuses to mint a federation session when the
// caller omits the FederatedPeer admittance witness.
//
// HS14 floor for fixy-CR-07 + fixy-A2-009 (the SharedPermission
// admittance witness on federation session mints).  Substrate signature
// is
//
//   template <typename Org, typename KeyTag = AnyFederationKey,
//             IsExecCtx Ctx, typename SenderEndpoint>
//   auto mint_sender(Ctx const&, SenderEndpoint&&,
//                    SharedPermission<FederatedPeer<Org>> admittance)
//
// Calling with three positional args (ctx, endpoint, /* no admittance */)
// fires an arity mismatch — there is no overload that takes only
// (ctx, endpoint) and the missing parameter has no default.  This is
// the load-bearing soundness rail: without the witness the local Cog
// cannot prove it was ever admitted to converse with `Org`, so the
// session-protocol mint must refuse.
//
// Expected diagnostic: GCC emits "no matching function for call to
// 'mint_sender(...)'" / "candidate function not viable: requires
// 3 arguments, but 2 were provided" / "too few arguments to function".

#include <crucible/fixy/Sess.h>

namespace fsess = crucible::fixy::sess;

struct NegFedNoAdmit_PeerOrg {};
struct NegFedNoAdmit_KeyTag {};

int main() {
    crucible::effects::BgCompileCtx ctx{};
    int endpoint = 0;

    // Missing the 3rd (admittance) argument — must fail arity check.
    auto bad = fsess::mint_sender<NegFedNoAdmit_PeerOrg,
                                  NegFedNoAdmit_KeyTag>(
        ctx, endpoint);
    (void)bad;
    return 0;
}
