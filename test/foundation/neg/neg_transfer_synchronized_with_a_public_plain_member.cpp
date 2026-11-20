// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class that states synchronized{} states that two threads can write an
// object of it at the same time (THE SHARED WRITE of foundation/Transfer.h).
// Its own code orders each private member, but a holder of a reference that
// is not const writes a public plain member with no order.  The walk checks
// the statement each time that it reads it, so the first question of a
// transfer property stops the build and names the member.

#include <foundation/Transfer.h>

namespace public_member {

struct[[= ::foundation::transfer::synchronized{}]] PublicCount {
    int count = 0;
};

}  // namespace public_member

static_assert(::foundation::transfer::Synchronized<public_member::PublicCount>);

int main() { return 0; }
