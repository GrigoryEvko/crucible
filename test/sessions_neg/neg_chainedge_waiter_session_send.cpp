// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-062 fixture #3 — WaiterProto is Recv-only.

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
    (void)sp;
    auto waiter = edge.waiter(std::move(wp));
    auto psh = ses::mint_chainedge_waiter_session<Edge>(waiter);
    [[maybe_unused]] auto bad =
        std::move(psh).send(waiter.expected_signal(), ses::signal_transport);
    return 0;
}
