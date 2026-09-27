// The address of an atomic machine cell is its identity: readers on
// other threads hold it.  AtomicMachineCell therefore asks that the cell
// derive from foundation::Pinned, and the handle holds the cell by lvalue
// reference.  A cell with the full read shape that is not Pinned can be
// copied, and a copy is a second cell that no reader watches.
//
// The protocol here is well-formed and runnable, and the cell has the
// state_type and the load, so only the Pinned clause can refuse it.

#include <fixy/session/MachineBridge.h>

#include <atomic>

namespace s = ::fixy::session;

namespace loose_cell_fixture {
struct Report {};

enum class Phase : unsigned char {
    Idle,
    Busy
};

struct LoosePhaseCell {
    using state_type = Phase;
    [[nodiscard]] Phase load(std::memory_order = std::memory_order_relaxed) const noexcept { return Phase::Idle; }
};
}  // namespace loose_cell_fixture

using Once = s::Send<loose_cell_fixture::Report, s::End>;

int main() {
    loose_cell_fixture::LoosePhaseCell cell{};
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto session = s::mint_atomic_session<Once>(ctx, cell);
    (void)session;
    return 0;
}
