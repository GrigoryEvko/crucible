// Tier 2, through the mistake a reader of fixy/Role.h is most likely to
// make: passing a ROLE where an ATOM belongs.
//
// A role is an alias for a whole binding — role::PureCopy<int> IS
// fn<int, atom::copy> — so writing it inside a pack asks for a binding
// whose grade on some axis is another binding.  It is not an atom: it is
// not final, it does not derive atom_base, and it names no axis.  Tier 2
// refuses it and names it.
//
// The gate that keeps a role honest is the other direction, and it is
// separate: IsRoleFor asks that the alias lands on an fn the gate itself
// admits, so a role cannot smuggle a pack past the tiers.  This fixture
// is the case where the two are confused at a call site.

#include <fixy/Fn.h>
#include <fixy/Role.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::role::PureCopy<int>> refused{};
    return 0;
}
