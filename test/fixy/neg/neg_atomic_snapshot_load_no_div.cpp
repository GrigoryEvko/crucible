// Negative compile test.  This file must fail to compile.
//
// AtomicSnapshot::load spins with no exit while a write is in flight, so it
// asks for a context that owns Div.  The foreground context of the hot path
// owns no atom, so it fails the gate.  try_load is the read with no wait.
//
// Expected diagnostic: CtxOwnsCapability of Div is not satisfied for the
// foreground context.

#include <fixy/Ctx.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

int main() {
    const ::fixy::HotFgCtx fg = ::foundation::effects::testing::foreground();
    const ::fixy::concurrent::AtomicSnapshot<std::uint64_t> snapshot{};
    return static_cast<int>(snapshot.load(fg));
}
