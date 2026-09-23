// A Security atom that the closed relation security_class_of_ does not
// name.
//
// Every reader of the Security grade asks that relation, and its primary
// is declared without a definition.  So an atom on the axis with no class
// stops the build at the first reader, rather than reading as public and
// letting the binding through.  The atom here is written the way a
// shipped one is, which the IsAtom recipe cannot tell apart, so the
// relation is the gate that refuses it.

#include <fixy/Fn.h>

struct unlisted_level final : ::fixy::atom::atom_of<::fixy::Axis::Security> {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, unlisted_level> refused{};
    return 0;
}
