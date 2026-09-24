// B001: a background observable surface that may run unbounded is a
// back-pressure trap.
//
// The producer cannot see the consumer fall behind, because the surface
// exists to report facts and not to apply back pressure — that is what
// makes it a trap rather than merely slow.  The remedy the theorem names
// is the pair space::Bounded plus cost::Linear: a bound on what the
// surface may hold and a bound on what it may spend.
//
// This is the theorem the catalog recorded for Axis::Observability before
// the axis had any atom, and it is NOT the containment rule.  B002 is the
// containment, and it is a new code rather than a rereading of this one,
// because a rule code here is stable API and this theorem has its own
// remedy.  The pack below satisfies B002 — Bg is in the effect row — and
// trips only B001, which is what shows the two are independent.
//
// "May run unbounded" reads three ways and the rule refuses all three: an
// explicit unbounded cost, an unstated cost, or an explicit unbounded
// space.  This pack uses the second, which is the one a caller reaches by
// writing nothing.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with<::foundation::effects::Effect::Bg>,
                                ::fixy::atom::observe::surface<::foundation::effects::Effect::Bg>>
        refused{};
    return 0;
}
