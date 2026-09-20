// The cell is a real AtomicMachineCell, so the only clause left to
// reject this call is the protocol's.
//
// Continue binds to the nearest enclosing Loop.  With no Loop above it
// the protocol has no next position, and a handle over the cell would
// have nowhere to step.

#include <fixy/session/MachineBridge.h>

#include <atomic>

namespace s = fixy::session;

namespace {
struct Report {};

enum class Phase : unsigned char {
    Idle,
    Busy
};

struct PhaseCell {
    using state_type = Phase;
    [[nodiscard]] Phase load(std::memory_order order = std::memory_order_relaxed) const noexcept {
        return value_.load(order);
    }

private:
    std::atomic<Phase> value_{Phase::Idle};
};
}  // namespace

using Orphan = s::Send<Report, s::Continue>;

int main() {
    PhaseCell cell{};
    auto session = s::mint_atomic_session<Orphan>(cell);
    (void)session;
    return 0;
}
