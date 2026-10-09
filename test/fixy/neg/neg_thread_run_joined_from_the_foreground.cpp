// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The thread door starts threads only for a context that owns Bg or Init,
// the authority to start threads.  The foreground context owns neither, so
// the door refuses the call and names the gate.
//
// Expected diagnostic: no candidate of run_joined, with a note on the gate
// of the context.

#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    auto body = []() noexcept {};
    [[maybe_unused]] auto started =
        ::fixy::thread::run_joined(::foundation::effects::testing::foreground(), ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
