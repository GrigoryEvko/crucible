// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// op_record_slots() hands out the slots into which the producer thread
// decodes the records of an op, so it asks for the foreground context of a
// Vigil's producer claim.  The context of another state's claim proves that
// its holder owns that state, not a Vigil.

#include <crucible/Vigil.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::Vigil vigil;
    crucible::OpRecordSlots& slots = vigil.op_record_slots(::foundation::effects::testing::foreground<Stranger>());
    (void)slots;
    return 0;
}
