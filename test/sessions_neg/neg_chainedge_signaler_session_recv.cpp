// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-062 fixture #4 — SignalerProto is Send-only.

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChainEdgeSession.h>

#include <utility>

namespace conc = ::crucible::concurrent;
namespace ses = ::crucible::safety::proto::chainedge_session;

namespace {
struct Tag {};
using Edge = conc::PermissionedChainEdge<conc::VendorBackend::CPU, Tag>;
}

int main() {
    Edge edge{conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{3}};
    auto whole = ::crucible::safety::mint_permission_root<Edge::whole_tag>();
    auto [sp, wp] = ::crucible::safety::mint_permission_split<
        Edge::signaler_tag, Edge::waiter_tag>(std::move(whole));
    (void)wp;
    auto signaler = edge.signaler(std::move(sp));
    auto psh = ses::mint_chainedge_signaler_session<Edge>(signaler);
    [[maybe_unused]] auto bad = std::move(psh).recv(ses::wait_transport);
    return 0;
}
