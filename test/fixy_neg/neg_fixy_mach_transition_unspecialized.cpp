// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-21 fixture #1: transition_to rejects when the (OldState,
// NewState) pair has no machine_transition specialization opting in.
//
// Violation: `transition_to<NewState>(Machine<OldState>&&, NewState)`
// requires `MachineTransition<OldState, NewState>` which folds the
// `machine_transition<From, To>` opt-in trait.  Without a
// CRUCIBLE_ALLOW_MACHINE_TRANSITION(From, To) declaration, the
// primary template defaults to std::false_type and the requires-
// clause rejects.
//
// Expected diagnostic: "associated constraints are not satisfied"
// / "MachineTransition" — the gate names the failing edge.

#include <crucible/safety/Machine.h>

namespace saf = crucible::safety;

struct Open {};
struct Closed {};

// NOTE: no CRUCIBLE_ALLOW_MACHINE_TRANSITION(Open, Closed) declared —
// the transition is the very thing the gate must reject.

int main() {
    auto m = saf::mint_machine<Open>();
    auto bad = saf::transition_to(std::move(m), Closed{});
    (void)bad;
    return 0;
}
