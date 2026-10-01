// The compile-time checks of fixy/atoms/Stack.h.

#include <fixy/atoms/Stack.h>

namespace fixy::atom::detail::stack_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::stack, stack_atom_roster>(),
              "fixy/atoms/Stack.h: an atom declared in fixy::atom::stack is missing from "
              "stack_atom_roster.");

static_assert(every_roster_member_is_atom_<stack_atom_roster>(),
              "fixy/atoms/Stack.h: a member of stack_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<stack_atom_roster, Axis::StackUse>(),
              "fixy/atoms/Stack.h: every stack-use atom engages Axis::StackUse.");

// The budget is part of the type.
static_assert(!std::is_same_v<stack::alloc<64>, stack::alloc<128>>);
static_assert(std::is_same_v<stack::alloc<64>, stack::alloc<64>>);

}  // namespace fixy::atom::detail::stack_atom_self_test
