// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// load_syscall_tp_btf takes the startup load context.  A background load
// context also claims Block, but its capability source is the background
// source.  No conversion makes it a startup load context, so the compiler
// rejects the call.

#include <crucible/perf/SyscallTpBtf.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
    ::fixy::Option<crucible::perf::SyscallTpBtf> hub = crucible::perf::load_syscall_tp_btf(background);
    (void)hub;
    return 0;
}
