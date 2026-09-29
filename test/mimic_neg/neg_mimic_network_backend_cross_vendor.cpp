// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/mimic/_wip/network/Backend.h>

namespace mb = crucible::mimic::_wip::network;

static void require_nv(mb::DeclaredNetworkKernel<mb::NetworkBackendVendor::Nv>) {}

int main() {
    const mb::DeclaredNetworkKernel<mb::NetworkBackendVendor::Am> am =
        ::fixy::mint_tagged<mb::wip_source::AmNetwork>(mb::NetworkKernelArtifact{});
    require_nv(am);
}
