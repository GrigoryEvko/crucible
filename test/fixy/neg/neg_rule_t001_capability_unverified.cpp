// T001: capability x trust::unverified.  A capability mints a
// non-revocable authorization token that consumers treat as proof of
// authority, and a binding of unverified provenance cannot establish
// that proof.
//
// Unverified is the strict pole of the Trust axis, so the atom is
// written out rather than defaulted.  That is deliberate: a binding
// that says nothing about Trust IS unverified, so the pack
// `fn<int, capability_usage>` alone trips T001 too.  Naming the atom
// puts the premise in the fixture's own text, where a reader sees which
// half of the rule each atom supplies.
//
// The pack trips T001 alone: no other live rule reads the Trust axis.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::capability_usage, ::fixy::atom::trust_unverified> refused{};
    return 0;
}
