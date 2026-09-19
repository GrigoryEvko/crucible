#pragma once

// The standard-io atoms: the stream a binding writes.  Every atom here
// engages Axis::Stdio.
//
// Old spelling: include/crucible/fixy/grant/Stdio.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::stdio {

// <cstdio> defines lowercase stderr and stdout as object-like macros. The
// capitalized spellings are immune to that expansion.
namespace streams {
struct Stderr final {};
struct Stdout final {};
struct Debug final {};
}  // namespace streams

template <class Stream>
struct write final : atom_of<Axis::Stdio> {};

}  // namespace fixy::atom::stdio

namespace fixy::atom::detail {

using stdio_atom_roster = std::tuple<stdio::write<stdio::streams::Stderr>, stdio::write<stdio::streams::Stdout>,
                                     stdio::write<stdio::streams::Debug>>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::stdio_atom_self_test {

static_assert(every_roster_member_is_atom_<stdio_atom_roster>(),
              "fixy/atoms/Stdio.h: a member of stdio_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<stdio_atom_roster, Axis::Stdio>(),
              "fixy/atoms/Stdio.h: every stdio atom engages Axis::Stdio.");

// The stream tags are arguments, never atoms, and the stream is part of
// the type.
static_assert(!IsAtom<stdio::streams::Stderr>);
static_assert(!IsAtom<stdio::streams::Stdout>);
static_assert(!IsAtom<stdio::streams::Debug>);
static_assert(!std::is_same_v<stdio::write<stdio::streams::Stderr>, stdio::write<stdio::streams::Stdout>>);
static_assert(!std::is_same_v<stdio::write<stdio::streams::Stdout>, stdio::write<stdio::streams::Debug>>);

}  // namespace fixy::atom::detail::stdio_atom_self_test
