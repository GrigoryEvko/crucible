// Each family roster is a hand list, and every_atom_in_is_rostered_
// reads the family namespace to catch an atom the list omits.  The five
// assertions in the check files of the atom headers all pass, so none of
// them shows the walk answering no.
//
// This plants a plain atom in two family namespaces ahead of their
// headers.  Both must be refused, and naming two families rather than
// one shows the check is per-namespace rather than a single site that
// happens to fire.
//
// The atoms are planted before the headers for the same reason the OS
// tag fixture does it: the walk is a template instantiated once, and
// its member list is fixed where it runs, so a class declared after the
// walk runs is not visible to it.  The headers hold no checks, so the
// fixture includes the check file of each header, which includes the
// header and runs its checks.

#include <fixy/Atom.h>

namespace fixy::atom::ctrl {
struct unrostered_control_flow final : atom_of<Axis::ControlFlow> {};
}  // namespace fixy::atom::ctrl

namespace fixy::atom::stack {
struct unrostered_stack_use final : atom_of<Axis::StackUse> {};
}  // namespace fixy::atom::stack

#include "../../layer/checks/fixy/atoms/Ctrl.cpp"
#include "../../layer/checks/fixy/atoms/Stack.cpp"

int main() { return 0; }
