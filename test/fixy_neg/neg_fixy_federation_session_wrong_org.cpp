// fixy_neg: mint_sender<OrgB> rejects an admittance token for OrgA.
//
// A federation mint takes its admittance as
// SharedPermission<FederatedPeer<Org>>, where Org is the organization of the
// session.  A token for OrgA does not convert to a token for OrgB, so an
// admittance to OrgA does not give a session to OrgB.
//
// The context row has IO and Block, the endpoint is per-pair FIFO, and the
// token for OrgA comes from a real admittance and a real pool.  Only the
// organization of the token is incorrect.
//
// Expected diagnostic: GCC finds no conversion for argument 3 from the token
// for OrgA to the token for OrgB.

#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Source.h>

#include <utility>

namespace fsess = crucible::fixy::sess;
namespace cs = crucible::safety;
namespace eff = crucible::effects;
namespace ff = crucible::fixy::source::federation;

// A federation mint needs IO and Block in the row of its context.
using FederationFitCtx =
    decltype(eff::BgCompileCtx{::crucible::effects::testing::bg()}
                 .in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>());

struct NegFedWrongOrg_OrgA {};
struct NegFedWrongOrg_OrgB {};
struct NegFedWrongOrg_Endpoint {
    static constexpr ::fixy::session::Network session_network = ::fixy::session::Network::PerPairFifo;
};

// mint_federation_admittance is deprecated.  The pragma keeps its warning out
// of the diagnostic that the regexes read.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = cs::mint_permission_root<ff::LocalCipherTag>();
    auto handshake_a = ff::make_self_signed_handshake<NegFedWrongOrg_OrgA>();
    auto admitted_a = ff::mint_federation_admittance<NegFedWrongOrg_OrgA>(local, handshake_a);
    auto pool_a = fsess::federation::mint_federation_pool<NegFedWrongOrg_OrgA>(std::move(*admitted_a));
    auto guard_a = pool_a.lend();

    FederationFitCtx ctx{::crucible::effects::testing::bg()};
    auto bad = fsess::mint_sender<NegFedWrongOrg_OrgB>(ctx, NegFedWrongOrg_Endpoint{}, guard_a->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
