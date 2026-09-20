// B002: an observability surface names an effect outside the binding's
// effect row.
//
// Observability is not a second effect row.  The Effect grade stays the
// single authority for what a binding may do, and Observability names
// which PART of that row is observation rather than computation.  A
// surface naming IO on a binding whose effect row is empty therefore
// claims an effect the operation never declared, and the claim would
// widen what the operation is permitted to do.
//
// That is the failure this rule exists for: observation that requires an
// effect the operation does not have is observation that changes
// behaviour.  CLAUDE.md L15 says Observe "records facts; it does not
// enforce policy", and a passive surface whose declaration can widen the
// operation is not passive.
//
// The twin of this fixture is accepted and lives in
// test/fixy/test_collision.cpp: add atom::with<Effect::IO> to the pack and
// the same surface passes, because the effect is then declared.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::observe::surface<::foundation::effects::Effect::IO>> refused{};
    return 0;
}
