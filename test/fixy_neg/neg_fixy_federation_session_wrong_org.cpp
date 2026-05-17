// fixy_neg: mint_sender<OrgA> rejects a Permission<FederatedPeer<OrgB>>
// admittance witness — the phantom Org tag on the witness must match
// the Org template parameter on the mint, closing the cross-org
// session-impersonation gap closed by fixy-CR-07.
//
// HS14 floor for fixy-CR-07.  Substrate signature is
//
//   template <typename Org, typename KeyTag = AnyFederationKey,
//             IsExecCtx Ctx, typename SenderEndpoint>
//   auto mint_sender(Ctx const&, SenderEndpoint&&,
//                    Permission<FederatedPeer<Org>> const& admittance)
//
// `Permission<Tag>` is move-only with phantom tag — there is no
// implicit conversion between Permission<FederatedPeer<OrgA>> and
// Permission<FederatedPeer<OrgB>>.  Holding an admittance to OrgA does
// not authorize a session to OrgB; the type system enforces this at
// compile time.
//
// Distinct from `neg_fixy_federation_session_no_admittance` (arity
// rail): this exercises the type-mismatch rail when admittance is
// present but for the wrong peer.
//
// Expected diagnostic: GCC emits "cannot bind reference of type
// 'const Permission<FederatedPeer<OrgB>>&' to
// 'Permission<FederatedPeer<OrgA>>'" or an equivalent overload
// resolution failure naming the substrate function signature.

#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Source.h>

namespace fsess = crucible::fixy::sess;
namespace cs    = crucible::safety;
namespace ff    = crucible::fixy::source::federation;

struct NegFedWrongOrg_OrgA {};
struct NegFedWrongOrg_OrgB {};

// CR-02/CR-03/CR-04 — mint_federation_admittance is [[deprecated]];
// suppress so it does not interleave with the expected cross-org
// admittance-tag-mismatch regex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    // Mint a legitimate admittance for OrgA.
    auto local = cs::mint_permission_root<ff::LocalCipherTag>();
    auto handshake_a =
        ff::make_self_signed_handshake<NegFedWrongOrg_OrgA>();
    auto admitted_a =
        ff::mint_federation_admittance<NegFedWrongOrg_OrgA>(
            local, handshake_a);

    crucible::effects::BgCompileCtx ctx{};
    int endpoint = 0;

    // Try to mint a session to OrgB using an OrgA admittance — the
    // Permission tags are distinct phantom types and must not bind.
    auto bad = fsess::mint_sender<NegFedWrongOrg_OrgB>(
        ctx, endpoint, *admitted_a);
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
