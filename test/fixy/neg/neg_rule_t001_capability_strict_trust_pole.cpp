// T001: capability x the strict Trust pole.
//
// A binding that says nothing about Trust sits at the strict pole,
// tags::trust::Unverified.  A capability mints a non-revocable
// authorization token, and a binding of unverified provenance cannot
// establish the authority that the token gives.  So T001 refuses the pack
// with no Trust atom, as it refuses the pack that states trust_unverified.
//
// Only T001 refuses the pack: no other live rule reads the Trust axis, and
// the payload claims no replay, so S011 does not apply.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::capability_usage> refused{};
    return 0;
}
