// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-H-21 fixture #2: transition_to is DIRECTIONAL.  Allowing
// (Authenticated -> SessionEstablished) does NOT also allow
// (SessionEstablished -> Authenticated) — the rollback edge needs
// its own opt-in.
//
// Violation: CRUCIBLE_ALLOW_MACHINE_TRANSITION(Authenticated,
// SessionEstablished) registers ONE direction.  The neg-fixture
// attempts the inverse, which the primary `machine_transition`
// template still rejects (default std::false_type).
//
// Expected diagnostic: "associated constraints are not satisfied"
// / "MachineTransition<SessionEstablished, Authenticated>".

#include <crucible/safety/Machine.h>

namespace saf = crucible::safety;

struct Authenticated {};
struct SessionEstablished {};

// Forward edge ONLY.
CRUCIBLE_ALLOW_MACHINE_TRANSITION(Authenticated, SessionEstablished)

int main() {
    auto m_auth = saf::mint_machine<Authenticated>();
    auto m_sess = saf::transition_to(std::move(m_auth), SessionEstablished{});

    // Rollback attempted without an inverse-direction specialization.
    auto bad = saf::transition_to(std::move(m_sess), Authenticated{});
    (void)bad;
    return 0;
}
