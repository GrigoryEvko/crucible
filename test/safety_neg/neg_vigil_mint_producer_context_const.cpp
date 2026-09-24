// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_producer_context claims the calling thread, so it writes the claim
// and cannot run through a const view of the Vigil.  A reader that holds
// only a const reference cannot take the producer role.

#include <crucible/Vigil.h>

namespace {
auto claim_through(const crucible::Vigil& vigil) { return vigil.mint_producer_context(); }
}  // namespace

int main() {
    crucible::Vigil vigil;
    (void)claim_through(vigil);
    return 0;
}
