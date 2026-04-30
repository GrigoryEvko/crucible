// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Wait<Park, T>` value to a function whose
// `requires` clause demands `Wait<SpinPause>::satisfies<...>` —
// the production hot-path admission gate.
//
// THE LOAD-BEARING REJECTION FOR THE no-park-on-hot-path discipline
// (28_04 §4.3.3 + CLAUDE.md §IX).  AtomicSnapshot::load_pinned()
// returns Wait<SpinPause, T>; hot-path consumers (SPSC ring waiters,
// shape-budgeted observers) require SpinPause-or-stronger.  A
// Wait<Park, T> value coming from a futex-backed wait MUST be
// rejected at the hot-path boundary — Park's ~1-5 μs wakeup
// latency is two orders of magnitude above the per-call shape
// budget (10-40 ns for a SpinPause MESI round-trip).
//
// Lattice direction (WaitLattice.h):
//     Block(weakest) ⊑ Park ⊑ AcquireWait ⊑ UmwaitC01 ⊑
//     BoundedSpin ⊑ SpinPause(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Park to satisfy
// SpinPause, we'd need leq(SpinPause, Park) — but SpinPause is
// STRONGER than Park, so that's FALSE.  The requires-clause rejects
// the call.
//
// Concrete bug-class this catches: a refactor that introduces a
// "convenience" overload accepting `T` raw and re-wrapping
// internally as Wait<Park, T> — bypassing the SpinPause hot-path
// fence.  Without this fixture, futex-backed waits would silently
// flow into TraceRing/Vigil/AtomicSnapshot consumer call sites
// budgeted at 10-40 ns per call.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/Wait.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-path observer that demands
// SpinPause tier.  Models the AtomicSnapshot::load_pinned ⇄
// hot-consumer pattern.
template <typename W>
    requires (W::template satisfies<WaitStrategy_v::SpinPause>)
static int spin_pause_hot_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Park — origin is a futex-backed waiter.  This is
    // what hot-path admission gates MUST reject.
    Wait<WaitStrategy_v::Park, int> park_value{42};

    // Should FAIL: spin_pause_hot_consumer requires SpinPause-or-
    // stronger; park_value carries Park, which is STRICTLY WEAKER
    // than SpinPause.  Without the requires-clause fence, futex-
    // waited values would silently flow into shape-budgeted hot-
    // path code, breaking the 10-40 ns per-call latency.
    int result = spin_pause_hot_consumer(std::move(park_value));
    return result;
}
