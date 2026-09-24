// An atom declared in a namespace that a user names fixy::atom, nested
// inside a namespace of the user's own.
//
// The spelling of the namespace matches the catalog.  parent_of gives
// the namespace itself, and that is a different namespace from
// ::fixy::atom.  The type has the whole shape of an atom, so the clause
// that refuses it is the catalog clause of IsAtom, and the compiler
// names that clause.

#include <fixy/Atom.h>

namespace user_code::fixy::atom {
struct forged_effect final : ::fixy::atom::atom_of<::fixy::Axis::Effect> {};
}  // namespace user_code::fixy::atom

static_assert(::fixy::atom::IsAtom<user_code::fixy::atom::forged_effect>);

int main() { return 0; }
