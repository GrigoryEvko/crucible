// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A policy with no audit-event slot loses each transition that it
// makes.  The class refuses a zero event count.

#include <crucible/warden/Quarantine.h>

using BadPolicy = crucible::warden::QuarantinePolicy<2, 0>;

int main() {
    (void)sizeof(BadPolicy);
    return 0;
}
