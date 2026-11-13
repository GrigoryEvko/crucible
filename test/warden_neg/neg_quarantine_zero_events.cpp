// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A policy with no audit-event slot loses each transition that it
// makes.  The constraint of the class refuses a zero event count.

#include <crucible/warden/Quarantine.h>

int main() {
    (void)sizeof(crucible::warden::QuarantinePolicy<2, 0>);
    return 0;
}
