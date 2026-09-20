// The gate on mint_vigil_mode_bridge is nominal, not structural.
//
// LookalikeCell satisfies AtomicMachineCell — it has the state_type and
// the load the concept asks for — so mint_atomic_session would accept
// it.  The mode bridge still refuses it, because the protocol's Send
// branches carry ModeTransition values that only the real cell knows
// how to publish.  Without the nominal gate a call would pick up the
// mode protocol over a cell that cannot perform it, and the refusal
// would arrive at the first send() instead of here.

#include <fixy/session/VigilMode.h>

#include <atomic>

namespace s = fixy::session;
namespace vm = fixy::session::vigil_mode;

namespace {
struct LookalikeCell {
    using state_type = vm::Mode;
    [[nodiscard]] vm::Mode load(std::memory_order = std::memory_order_relaxed) const noexcept {
        return vm::Mode::RECORDING;
    }
};
static_assert(s::AtomicMachineCell<LookalikeCell>, "the fixture is pointless unless the lookalike really does "
                                                   "satisfy the structural concept");
}  // namespace

int main() {
    LookalikeCell cell{};
    auto session = vm::mint_vigil_mode_bridge(cell);
    (void)session;
    return 0;
}
