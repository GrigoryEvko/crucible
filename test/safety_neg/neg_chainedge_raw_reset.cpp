// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ChainEdge reset also mutates semaphore state.  It must be gated by the
// Whole Permission through PermissionedChainEdge::reset_under_quiescence.

#include <crucible/concurrent/ChainEdge.h>

namespace conc = ::crucible::concurrent;

int main() {
    conc::ChainEdge<conc::VendorBackend::CPU> edge{
        conc::PlanId{1}, conc::PlanId{2}, conc::ChainEdgeId{3}};
    edge.reset_under_quiescence();
    return 0;
}
