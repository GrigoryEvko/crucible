// Negative compile test.  This file must fail to compile.
//
// The mint of an owned thread starts a thread only for a context that owns
// Bg or Init, the authority to start threads.  The foreground context owns
// neither, so the mint refuses the call and names its gate.
//
// Expected diagnostic: no candidate of mint_owned_thread, with a note on the
// gate of the mint.

#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

int main() {
    [[maybe_unused]] auto started = ::fixy::thread::mint_owned_thread(
        ::foundation::effects::testing::foreground(), ::fixy::thread::ThreadOptions{}, []() noexcept {});
    return 0;
}
