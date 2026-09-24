// H003: hot x an Alloc or IO row x unbounded cost.
//
// An allocation or an I/O call on the hot path is survivable when its
// cost is bounded — the memory plan of CLAUDE.md L3 is exactly that, one
// bump against a pre-sized pool.  What cannot be survived is the pair:
// an unbounded surface reached from a path budgeted in nanoseconds.  The
// remedy is to move the surface out, or to give it an Init or Bg context
// that owns the unboundedness.
//
// This pack trips H001 as well, and cannot avoid it: cost_unbounded is
// H001's own premise.  The tier-5 message lists every code a pack trips,
// so the fixture still floors on H003 specifically.  Block is deliberately
// NOT in this rule — a blocking hot path is W001's theorem, which waits
// on the Synchronization axis.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

namespace {

struct arena_capacity_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::with<::foundation::effects::Effect::Alloc>,
                                ::fixy::atom::cost_unbounded, ::fixy::atom::refined_with<arena_capacity_proved>>
        refused{};
    return 0;
}
