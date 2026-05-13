// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/mimic/_wip/network/Backend.h>

namespace mb = crucible::mimic::_wip::network;

void require_nv(mb::DeclaredNetworkKernel<mb::NetworkBackendVendor::Nv>) {}

int main() {
    mb::DeclaredNetworkKernel<mb::NetworkBackendVendor::Am> am{
        mb::NetworkKernelArtifact{}};
    require_nv(am);
}
