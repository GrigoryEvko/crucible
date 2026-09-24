// W001: hot x a kernel wait.
//
// A park or a futex wait costs 1-5 us because it is bounded by the
// scheduler.  A hot path is bounded by the cache-coherence fabric at
// 10-40 ns intra-socket.  Two orders of magnitude apart, which is why
// CLAUDE.md IX bans the kernel wait on the hot path outright and the
// hot-path wait is a spin on an acquire load.
//
// W001 reads a tier AND a wait strategy, so it fires only when both
// Axis::Regime and Axis::Synchronization carry atoms, and it is Pending
// while either axis has none.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// W001 alone.  atom::sync::acquire_wait is the cheapest of the three
// kernel grades, chosen deliberately: if even the cheapest is refused,
// the rule is not tuned to the worst case.

#include <fixy/Fn.h>

namespace {

struct ring_depth_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::sync::acquire_wait,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<ring_depth_proved>>
        refused{};
    return 0;
}
