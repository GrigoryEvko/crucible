// mint_atomic_session borrows a cell another thread publishes into, so
// it demands the read shape every such cell has: a state_type, and a
// load(memory_order) returning it.
//
// A plain struct has neither.  The protocol here is well-formed and
// runnable, so only the AtomicMachineCell clause can reject this call.

#include <fixy/session/MachineBridge.h>

namespace s = fixy::session;

namespace {
struct Report {};

// No state_type, no load.
struct NotACell {
    int value = 0;
};
}  // namespace

using Once = s::Send<Report, s::End>;

int main() {
    NotACell cell{};
    auto session = s::mint_atomic_session<Once>(cell);
    (void)session;
    return 0;
}
