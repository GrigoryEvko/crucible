// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ChainEdge raw semaphore mutation is substrate-only.  Production code must
// carry the Signaler Permission inside PermissionedChainEdge::SignalerHandle.

#include <crucible/concurrent/ChainEdge.h>

namespace conc = ::crucible::concurrent;

int main() {
    conc::ChainEdge<conc::VendorBackend::CPU> edge{
        conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{3}};
    edge.signal(edge.expected_signal());
    return 0;
}
