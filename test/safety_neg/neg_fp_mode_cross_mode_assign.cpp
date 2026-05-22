// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning FpRoundingPinned<RoundToZero, int> from
// FpRoundingPinned<RoundToNearestEven, int> — DIFFERENT pinned modes
// instantiate to DIFFERENT class types (the NTTP differs), so the
// implicit copy/move conversion is rejected at the C++ type level.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-090's
// load-bearing claim is "every pinned FP mode is type-distinct from
// every other pinned mode on the same axis", so DetSafe federation
// cache routing cannot accidentally route RoundToNearestEven results
// into the RoundToZero slot.  This fixture pins that the type system
// catches the cross-mode confusion at compile time — a contributor
// who relaxes FpModePinned's NTTP-based type-distinctness (e.g., by
// adding an `operator=(FpModePinned<OtherMode, T> const&)` overload)
// will silently break Lemma 7(b)'s per-axis numerics contract, and
// this fixture is the test that catches it.
//
// Concrete bug-class this catches: a contributor "helpfully" adds a
// cross-mode constructor or assignment so RoundToNearestEven can
// "auto-convert" to RoundToZero — defeating the entire point of the
// type-level pinning.  Two pinned modes on the same axis MUST never
// be interconvertible without an explicit user-side relax/repin
// transition (which V-090 also intentionally does NOT provide; see
// the FpMode.h §"Why NO relax<>/satisfies<>" doc-block).
//
// Pairs with neg_fp_mode_cross_axis_pass.cpp for the 2-fixture HS14
// floor — one fixture per distinct mismatch class:
//   1. cross_mode_assign: same axis, different mode → assign reject (this).
//   2. cross_axis_pass:   different axis → function-arg type reject.
//
// Substring "cannot convert" / "no match" / "deleted function" pins
// the diagnostic family — the precise text varies with GCC version
// but the structural shape (class-type cross-conversion impossible)
// is invariant.

#include <crucible/safety/FpMode.h>

int main() {
    using namespace crucible::safety;

    // Should FAIL: cross-mode assignment requires inter-class conversion
    // between two distinct instantiations of FpModePinned.
    FpRoundingPinned<FpRounding::RoundToNearestEven, int> src{7};
    FpRoundingPinned<FpRounding::RoundToZero,         int> dst{0};
    dst = src;
    (void)dst;
    return 0;
}
