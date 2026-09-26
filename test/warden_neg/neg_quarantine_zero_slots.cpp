// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A policy with no Cog slot can hold no state.  The class refuses a zero
// slot count.  The event count is explicit, because its default is four
// times the slot count and a zero there fails a second assertion.

#include <crucible/warden/Quarantine.h>

using BadPolicy = crucible::warden::QuarantinePolicy<0, 4>;

int main() {
    (void)sizeof(BadPolicy);
    return 0;
}
