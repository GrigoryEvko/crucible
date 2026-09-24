// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ring() hands out the producer surface of the ring, so it asks for the
// foreground context of a Vigil's producer claim.  The context of another
// state's claim proves that its holder owns that state, not a Vigil.

#include <crucible/Vigil.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing& ring = vigil.ring(::foundation::effects::testing::foreground<Stranger>());
    (void)ring;
    return 0;
}
