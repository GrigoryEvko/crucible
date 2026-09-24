// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A load that puts the schema names of a trace into the global table asks
// for the foreground context of a Vigil's producer claim.  The context of
// another state's claim proves that its holder owns that state, not a Vigil.

#include <crucible/TraceLoader.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    auto trace = crucible::load_trace(::foundation::effects::testing::foreground<Stranger>(), "trace.crtrace");
    (void)trace;
    return 0;
}
