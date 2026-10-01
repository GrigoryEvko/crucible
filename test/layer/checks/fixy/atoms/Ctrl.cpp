// The compile-time checks of fixy/atoms/Ctrl.h.

#include <fixy/atoms/Ctrl.h>

namespace fixy::atom::detail::ctrl_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::ctrl, ctrl_atom_roster>(),
              "fixy/atoms/Ctrl.h: an atom declared in fixy::atom::ctrl is missing from "
              "ctrl_atom_roster.");

static_assert(every_roster_member_is_atom_<ctrl_atom_roster>(),
              "fixy/atoms/Ctrl.h: a member of ctrl_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<ctrl_atom_roster, Axis::ControlFlow>(),
              "fixy/atoms/Ctrl.h: every control-flow atom engages Axis::ControlFlow.");

static_assert(!IsAtom<ctrl::any_exception>);
static_assert(!IsAtom<ctrl::at_exit>);
static_assert(!IsAtom<ctrl::co_await_only>);

// The rationale is part of the type: two reasons are two types, the
// same reason is one type, and a prefix is not the whole.
static_assert(!std::is_same_v<ctrl::abort<"reason A">, ctrl::abort<"reason B">>);
static_assert(std::is_same_v<ctrl::abort<"same">, ctrl::abort<"same">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);
static_assert(!std::is_same_v<ctrl::abort<"x">, ctrl::longjmp_unsafe<"x">>);
static_assert(ctrl::rationale{"oom"}.size() == 4);  // three characters plus the terminator

}  // namespace fixy::atom::detail::ctrl_atom_self_test
