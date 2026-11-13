// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A policy with no Cog slot can hold no state.  The constraint of the class
// refuses a zero slot count.  The event count is explicit, because its
// default is four times the slot count, and a zero there is a second refusal.

#include <crucible/warden/Quarantine.h>

int main() {
    (void)sizeof(crucible::warden::QuarantinePolicy<0, 4>);
    return 0;
}
