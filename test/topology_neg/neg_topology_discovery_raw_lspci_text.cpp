// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The lspci parser takes only text tagged as External.  A raw string does
// not convert to the tagged type, so untrusted text cannot reach the parser
// without a caller naming its source.

#include <crucible/topology/Discovery.h>

int main() {
    ::fixy::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = crucible::topology::mint_discovery_snapshot(ctx);
    auto parsed = crucible::topology::parse_lspci_vmm_tree("Slot:\t0000:00:00.0\n", snapshot);
    (void)parsed;
    return 0;
}
