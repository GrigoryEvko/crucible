// fixy_neg: mint_federation_channel rejects a non-IsExecCtx first
// argument.
//
// HS14 floor for fixy::sess::mint_federation_channel (fixy/Sess.h
// §B6 forwarder).  The forwarder threads `Ctx const&` into
// federation::mint_channel, whose own template parameter is constrained
// `IsExecCtx Ctx`.  Passing a plain `int` as the ctx slot fires the
// constraint-satisfaction failure inside federation::mint_channel.
//
// fixy-CR-07 + fixy-A2-009: federation mints now also take a
// `SharedPermission<FederatedPeer<Org>>` admittance witness (by value)
// as the final argument.  The exclusive `Permission<FederatedPeer<Org>>`
// parks in a `SharedPermissionPool` once at admission; per-call sites
// borrow a guard via `lend()` and pass `guard->token()`.  We bootstrap
// a legitimate admittance + pool here so the call site is syntactically
// well-formed; the IsExecCtx<Ctx> constraint failure on the first arg
// is what fires.
//
// Note: we include only fixy/Sess.h and fixy/Source.h (NOT
// FederationPermission.h directly) to avoid the GCC 16.1.1 ICE in
// cp_fold_r on Refined.h via the federation path — fixy re-exports the
// federation symbols via namespace alias.
//
// Expected diagnostic: "IsExecCtx" — constraint-satisfaction failure
// from federation::mint_channel's Ctx template parameter.

#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Source.h>

#include <utility>

namespace fsess = crucible::fixy::sess;
namespace cs    = crucible::safety;
namespace ff    = crucible::fixy::source::federation;

struct NegFedChannelNonCtx_PeerOrg {};

// CR-02/CR-03/CR-04 — mint_federation_admittance is [[deprecated]];
// suppress the diagnostic so it does not interleave with the expected
// IsExecCtx regex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = cs::mint_permission_root<ff::LocalCipherTag>();
    auto handshake = ff::make_self_signed_handshake<
        NegFedChannelNonCtx_PeerOrg>();
    auto admitted = ff::mint_federation_admittance<
        NegFedChannelNonCtx_PeerOrg>(local, handshake);
    auto pool = fsess::federation::mint_federation_pool<
        NegFedChannelNonCtx_PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    int not_a_ctx = 0;
    // Plain int as ctx — fails IsExecCtx constraint at template
    // parameter substitution time; the 4-arg shape is satisfied so
    // arity-check passes and the constraint check is what surfaces.
    auto bad = fsess::mint_federation_channel<
        NegFedChannelNonCtx_PeerOrg>(
        not_a_ctx, 0, 0, guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
