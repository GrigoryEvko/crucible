// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Crash<Abort, T>` value to a function whose
// `requires` clause demands `Crash<ErrorReturn>::satisfies<...>` —
// the recovery-handler boundary.
//
// THE LOAD-BEARING REJECTION FOR THE recovery-aware boundary
// discipline (28_04 §4.3.10).  The ErrorReturn class declares
// "this call may return an error via std::expected, but will not
// kill the process".  An Abort-classified value comes from a path
// that may have called crucible_abort() — admitting it into the
// recovery branch would defeat the recovery code's right-to-run
// invariant (recovery code has the right to assume the process
// is still alive when it executes).
//
// Lattice direction (CrashLattice.h):
//     Abort(weakest) ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Abort to satisfy
// ErrorReturn, we'd need leq(ErrorReturn, Abort) — but ErrorReturn
// is STRONGER than Abort, so that's FALSE.  The requires-clause
// rejects the call.
//
// Concrete bug-class this catches: a Keeper init/shutdown path
// that may have called crucible_abort() (Abort tier) is
// accidentally routed through a recovery-handler harness expecting
// ErrorReturn.  Without this fence, the recovery code would
// silently run on top of a possibly-already-dead process state.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of Abort-into-ErrorReturn.

#include <crucible/safety/Crash.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: recovery-handler that demands
// ErrorReturn-or-stronger.  Models the OneShotFlag::try_
// acknowledge_error_return ⇄ recovery-pipeline pattern.
template <typename W>
    requires (W::template satisfies<CrashClass_v::ErrorReturn>)
static bool error_return_recovery_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    Crash<CrashClass_v::Abort, bool> abort_value{true};

    // Should FAIL: error_return_recovery_consumer requires
    // ErrorReturn-or-stronger; abort_value carries Abort, which is
    // STRICTLY WEAKER than ErrorReturn.  Without the requires-
    // clause fence, abort-prone values would silently flow into
    // recovery code that assumes the process is still alive.
    bool result = error_return_recovery_consumer(std::move(abort_value));
    return result ? 0 : 1;
}
