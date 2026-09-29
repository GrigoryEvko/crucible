// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Senses::load_all takes the startup load context.  A background load
// context also claims Block, but its capability source is the background
// source.  No conversion makes it a startup load context, so the compiler
// rejects the call.

#include <crucible/perf/Senses.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
    auto s = crucible::perf::Senses::load_all(background);
    (void)s;
    return 0;
}
