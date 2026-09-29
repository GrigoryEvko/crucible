// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A load reads a file, so it asks for a context that owns IO and Block.
// The foreground context of a Vigil's producer claim owns neither, so the
// foreground thread cannot load a trace.

#include <crucible/TraceLoader.h>

int main() {
    auto trace = crucible::load_trace(::foundation::effects::testing::foreground<crucible::Vigil>(), "trace.crtrace");
    (void)trace;
    return 0;
}
