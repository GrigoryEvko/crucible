#pragma once

// The global-state atoms: the process-wide state a binding touches.
// Every atom here engages Axis::GlobalState.
//
// Each distinct global needs its own tag type. Two globals that share one
// tag collapse to a single atom and become indistinguishable to any
// consumer that walks the atoms of a binding.
//
// Old spelling: include/crucible/fixy/grant/Global.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::global {

template <class GlobalTag>
struct singleton final : atom_of<Axis::GlobalState> {};

template <class TLSTag>
struct thread_local_ final : atom_of<Axis::GlobalState> {};

template <class StaticTag>
struct namespace_static final : atom_of<Axis::GlobalState> {};

struct atexit_handler final : atom_of<Axis::GlobalState> {};

}  // namespace fixy::atom::global

namespace fixy::atom::detail {

struct global_sample_tag final {};

using global_atom_roster = std::tuple<global::singleton<global_sample_tag>, global::thread_local_<global_sample_tag>,
                                      global::namespace_static<global_sample_tag>, global::atexit_handler>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::global_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::global, global_atom_roster>(),
              "fixy/atoms/Global.h: an atom declared in fixy::atom::global is missing from "
              "global_atom_roster.");

static_assert(every_roster_member_is_atom_<global_atom_roster>(),
              "fixy/atoms/Global.h: a member of global_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<global_atom_roster, Axis::GlobalState>(),
              "fixy/atoms/Global.h: every global-state atom engages Axis::GlobalState.");

// The tag is part of the type, and the three parametric atoms stay
// distinct over one tag.
struct other_tag final {};
static_assert(!std::is_same_v<global::singleton<global_sample_tag>, global::singleton<other_tag>>);
static_assert(!std::is_same_v<global::singleton<global_sample_tag>, global::thread_local_<global_sample_tag>>);
static_assert(!std::is_same_v<global::thread_local_<global_sample_tag>, global::namespace_static<global_sample_tag>>);

}  // namespace fixy::atom::detail::global_atom_self_test
