// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-062 fixture #2 — ChainEdge waiter endpoints wait only.

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace {
struct Tag {};
using Edge = ::crucible::concurrent::PermissionedChainEdge<
    ::crucible::concurrent::VendorBackend::CPU, Tag>;
}

int main() {
    Edge edge{::crucible::concurrent::PlanId{1},
              ::crucible::concurrent::PlanId{2},
              ::crucible::concurrent::ChainEdgeId{3}};
    auto whole = ::crucible::safety::mint_permission_root<Edge::whole_tag>();
    auto [sp, wp] = ::crucible::safety::mint_permission_split<
        Edge::signaler_tag, Edge::waiter_tag>(std::move(whole));
    (void)sp;
    auto waiter = edge.waiter(std::move(wp));
    waiter.signal(waiter.expected_signal());
    return 0;
}
