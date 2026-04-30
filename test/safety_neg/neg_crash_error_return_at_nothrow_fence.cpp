// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Crash<ErrorReturn, T>` value to a function
// whose `requires` clause demands `Crash<NoThrow>::satisfies<...>`
// — the steady-state hot-path observer boundary.
//
// THE LOAD-BEARING REJECTION FOR THE NoThrow-only steady-state
// admission discipline (28_04 §4.3.10).  The NoThrow class declares
// "this call NEVER fails — never returns an error, never throws,
// never aborts".  Hot-path observers (e.g., the relaxed atomic peek
// inside the SPSC ring's shape-budget critical path) MUST reject
// values from any class that admits failure modes — including
// ErrorReturn (which by definition may return std::expected
// errors).  Without this fence, error-returning values would
// silently flow into shape-budgeted hot-path code, requiring it to
// add (and not forget) error handling on a path that was supposed
// to be 1-cycle-per-op.
//
// Lattice direction (CrashLattice.h):
//     Abort(weakest) ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow(strongest)
//
// satisfies<Required> = leq(Required, Self).  For ErrorReturn to
// satisfy NoThrow, we'd need leq(NoThrow, ErrorReturn) — but
// NoThrow is STRONGER than ErrorReturn, so that's FALSE.  The
// requires-clause rejects the call.
//
// Concrete bug-class this catches: a refactor that lifts a
// recovery-handler return value (ErrorReturn) into a hot-path
// observer that previously consumed only peek_nothrow() values.
// The new flow would silently break the per-call shape budget —
// the consumer would compile because nothing checks the class —
// without this fixture nailing it down.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of ErrorReturn-into-NoThrow.

#include <crucible/safety/Crash.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-path NoThrow steady-state observer.
// Models the SPSC ring's shape-budget critical path — admits only
// values guaranteed not to fail.
template <typename W>
    requires (W::template satisfies<CrashClass_v::NoThrow>)
static bool nothrow_hot_path_observer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    Crash<CrashClass_v::ErrorReturn, bool> recovery_value{true};

    // Should FAIL: nothrow_hot_path_observer requires NoThrow;
    // recovery_value carries ErrorReturn, which is STRICTLY WEAKER
    // than NoThrow.  Without the requires-clause fence, recovery-
    // returning values would silently flow into shape-budgeted hot-
    // path code, breaking the "this call NEVER fails" guarantee.
    bool result = nothrow_hot_path_observer(std::move(recovery_value));
    return result ? 0 : 1;
}
