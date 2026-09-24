// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A load that puts the schema names of a trace into the global table asks
// for the context of a Vigil's producer claim.  A context that names no
// claim is refused.  A load with no context leaves the names out.

#include <crucible/TraceLoader.h>

int main() {
    auto trace = crucible::load_trace(::foundation::effects::testing::foreground(), "trace.crtrace");
    (void)trace;
    return 0;
}
