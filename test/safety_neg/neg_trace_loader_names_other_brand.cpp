// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A load that puts the schema names of a trace into the global table asks
// for the foreground context of a Vigil's producer claim.  The context of
// another state's claim proves that its holder owns that state, not a Vigil.

#include <crucible/TraceLoader.h>
#include <fixy/Ctx.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx io{::foundation::effects::testing::test()};
    auto trace = crucible::load_trace(io, ::foundation::effects::testing::foreground<Stranger>(), "trace.crtrace");
    (void)trace;
    return 0;
}
