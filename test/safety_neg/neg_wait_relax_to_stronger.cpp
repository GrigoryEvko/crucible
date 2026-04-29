// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Wait<WeakerStrategy, T>::relax<StrongerStrategy>()
// when StrongerStrategy > WeakerStrategy in the WaitLattice.
//
// THE LOAD-BEARING REJECTION FOR HOT-PATH WAITER ADMISSION.  Without
// it, a value sourced from a Park (futex, μs latency) or Block
// (syscall) context could be re-typed as SpinPause-bound and silently
// flow into a hot-path SPSC ring polling loop, defeating the per-call
// shape budget (CLAUDE.md §IX.5: 10-40ns wait floor on _mm_pause).
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `WaitLattice::leq(WeakerStrategy, Strategy)` to a permissive form
// — would silently allow a Park-tier value (carrying a futex
// dependency) to claim SpinPause compliance, defeating the hot-path
// waiter discipline.  The dispatcher's hot-path admission gate
// (per 28_04 §6.4) would then admit the value into a TraceRing
// try_pop loop, silently introducing futex syscall on the foreground
// path.
//
// Lattice direction: SpinPause is at the TOP (cheapest wait, ~10-40ns
// via MESI); Block is at the BOTTOM (most expensive, syscall-based).
// Going DOWN (SpinPause → BoundedSpin → ... → Block) is allowed —
// stronger wait-discipline trivially serves weaker requirement.
// Going UP is FORBIDDEN — would CLAIM more wait-discipline than the
// source provides.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/Wait.h>

using namespace crucible::safety;

int main() {
    // Pinned at Park — bytes derive from a pthread_cond_wait-based
    // sync point.  This is what hot-path waiter sites MUST reject;
    // the relax<> below is the bug-introduction path the wrapper
    // fences.
    Wait<WaitStrategy_v::Park, int> park_value{42};

    // Should FAIL: relax<SpinPause> on a Park-pinned wrapper.  The
    // requires-clause `WaitLattice::leq(SpinPause, Park)` is FALSE
    // — SpinPause is above Park in the chain — so the relax<>
    // overload is excluded.  Without this fence, a futex-backed
    // value could claim SpinPause compliance and silently enter a
    // hot-path ring poll, breaking the 10-40ns wait floor.
    auto spin_claim = std::move(park_value).relax<WaitStrategy_v::SpinPause>();
    return spin_claim.peek();
}
