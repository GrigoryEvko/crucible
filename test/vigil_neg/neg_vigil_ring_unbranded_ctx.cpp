// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The ring is single-producer, and ring() hands out its producer surface.
// So ring() asks for the context of this Vigil's producer claim, and a
// context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    crucible::TraceRing& ring = vigil.ring(::foundation::effects::testing::foreground());
    (void)ring;
    return 0;
}
