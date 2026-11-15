// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// exit_now ends the process with no exit hook, which only the child of a
// test does.  The foreground context owns no Test capability, so the gate
// CtxFitsChildProcess refuses it at the call.
//
// Expected diagnostic: exit_now has no viable candidate, and the note names
// the gate.

#include <fixy/os/Process.h>

namespace eff = foundation::effects;

int main() {
    auto const ctx = eff::testing::foreground();
    fixy::process::exit_now(ctx, 0);
}
