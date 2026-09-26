// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of QuarantinePolicy is private, and
// mint_quarantine_policy is its one friend.  A policy built from a
// configuration alone goes around the context gate of the mint.

#include <crucible/warden/Quarantine.h>

int main() {
    crucible::warden::QuarantinePolicy<2> policy{crucible::warden::QuarantineConfig{}};
    (void)policy;
    return 0;
}
