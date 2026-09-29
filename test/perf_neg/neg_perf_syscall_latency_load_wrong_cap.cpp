// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SyscallLatency::load takes the startup load context.  A background
// load context also claims Block, but its capability source is the
// background source.  No conversion makes it a startup load context, so
// the compiler rejects the call.

#include <crucible/perf/SyscallLatency.h>
#include <fixy/Ctx.h>

#include <optional>

int main() {
    const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
    std::optional<crucible::perf::SyscallLatency> hub = crucible::perf::SyscallLatency::load(background);
    (void)hub;
    return 0;
}
