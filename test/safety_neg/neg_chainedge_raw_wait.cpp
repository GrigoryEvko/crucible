// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ChainEdge raw semaphore observation is substrate-only.  Production code must
// carry the Waiter Permission inside PermissionedChainEdge::WaiterHandle.

#include <crucible/concurrent/ChainEdge.h>

namespace conc = ::crucible::concurrent;

int main() {
    conc::ChainEdge<conc::VendorBackend::CPU> edge{
        conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{3}};
    [[maybe_unused]] bool ok = edge.wait(edge.expected_signal());
    return 0;
}
