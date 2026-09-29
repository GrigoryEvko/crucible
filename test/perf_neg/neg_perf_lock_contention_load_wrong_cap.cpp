// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// LockContention::load takes the startup load context.  A background
// load context also claims Block, but its capability source is the
// background source.  No conversion makes it a startup load context, so
// the compiler rejects the call.

#include <crucible/perf/LockContention.h>
#include <fixy/Ctx.h>

#include <optional>

int main() {
    const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
    std::optional<crucible::perf::LockContention> hub = crucible::perf::LockContention::load(background);
    (void)hub;
    return 0;
}
