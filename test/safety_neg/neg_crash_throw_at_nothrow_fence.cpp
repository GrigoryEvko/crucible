// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Crash<Throw, T>` value to a function whose
// `requires` clause demands `Crash<NoThrow>::satisfies<...>` — the
// production NoThrow steady-state admission gate.
//
// THE LOAD-BEARING REJECTION FOR THE OneShotFlag-guarded boundary
// DISCIPLINE (28_04_2026_effects.md §4.3.10).  OneShotFlag::peek_
// nothrow() returns Crash<NoThrow, bool>; the steady-state consumer
// requires NoThrow class.  A Crash<Throw, ...> value coming from a
// signal-side producer MUST be rejected at the steady-state boundary
// — admitting Throw values into NoThrow contexts would defeat the
// hot-path "this call NEVER fails" guarantee that hot-path admission
// gates depend on.
//
// Lattice direction (CrashLattice.h):
//     Abort(weakest) ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Throw to satisfy
// NoThrow, we'd need leq(NoThrow, Throw) — but NoThrow is STRONGER
// than Throw, so leq(NoThrow, Throw) is FALSE.  The requires-clause
// rejects the call.
//
// Concrete bug-class this catches: a refactor that replaces a
// peek_nothrow() call site with the old peek() (returning bare bool)
// and rewraps as `Crash<Throw, bool>` to pass through a different
// audit harness — silently losing the NoThrow-only discipline.  The
// requires-clause rejects this drift at the boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-class flow.

#include <crucible/safety/Crash.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-path NoThrow steady-state observer
// that demands NoThrow class.  Models OneShotFlag::peek_nothrow ⇄
// consumer pattern.
template <typename W>
    requires (W::template satisfies<CrashClass_v::NoThrow>)
static bool nothrow_steady_state_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Throw — origin is a producer-side signal that may
    // raise the crash signal.  This is what NoThrow-only admission
    // gates MUST reject.  The wrapper carries the class; the
    // consumer's requires clause sees Throw doesn't satisfy NoThrow
    // (NoThrow ⊐ Throw in the lattice) and excludes the overload.
    Crash<CrashClass_v::Throw, bool> throw_value{true};

    // Should FAIL: nothrow_steady_state_consumer requires NoThrow;
    // throw_value carries Throw, which is STRICTLY WEAKER than
    // NoThrow.  Without the requires-clause fence, signal-side
    // values would silently flow into hot-path observation gates.
    bool result = nothrow_steady_state_consumer(std::move(throw_value));
    return result ? 0 : 1;
}
