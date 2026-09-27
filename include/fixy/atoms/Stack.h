#pragma once

// The stack-use atoms.  Every atom here engages Axis::StackUse.
//
// The strict default on this axis is the frame budget the build enforces
// with -Wframe-larger-than=4096. A binding needs an atom here only once it
// exceeds that budget.
//
// The family has no atom for a variable-length array or for alloca.
// The build refuses both constructs with -Wvla and -Walloca under
// -Werror, whatever a binding states.  An atom that names one would state
// a thing that the build forbids.
//
// Old spelling: include/crucible/fixy/grant/Stack.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace fixy::atom::stack {

inline constexpr atom_seal atom_namespace_seal{};

template <std::size_t MaxBytes>
struct alloc final : atom_of<Axis::StackUse> {};

}  // namespace fixy::atom::stack

namespace fixy::atom::detail {

using stack_atom_roster = std::tuple<stack::alloc<64>, stack::alloc<4096>>;

}  // namespace fixy::atom::detail

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
