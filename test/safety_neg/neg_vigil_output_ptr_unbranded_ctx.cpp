// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An output pointer of the replayed op is read only by the thread that
// holds this Vigil's producer claim, and mint_producer_context is the
// route to that context.  A context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    void* out = vigil.output_ptr(::foundation::effects::testing::foreground(), 0);
    (void)out;
    return 0;
}
