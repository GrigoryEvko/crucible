#pragma once

// The standard-io atoms: the stream a binding writes.  Every atom here
// engages Axis::Stdio.
//
// A write lifts to Row<IO, Block>.  A buffered stdio write takes the
// lock of the stream, and a flush writes to a descriptor that can be a
// pipe or a terminal, so the write can park the caller.  The lift puts
// both effects in the row that a binding requires of its context.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::stdio {

inline constexpr atom_seal atom_namespace_seal{};

// <cstdio> defines lowercase stderr and stdout as object-like macros. The
// capitalized spellings are immune to that expansion.
namespace streams {
struct Stderr final {};
struct Stdout final {};
struct Debug final {};
}  // namespace streams

template <class Stream>
struct write final
    : lifting_atom_of<Axis::Stdio,
                      ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>> {};

}  // namespace fixy::atom::stdio

namespace fixy::atom::detail {

using stdio_atom_roster = std::tuple<stdio::write<stdio::streams::Stderr>, stdio::write<stdio::streams::Stdout>,
                                     stdio::write<stdio::streams::Debug>>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::stdio_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::stdio, stdio_atom_roster>(),
              "fixy/atoms/Stdio.h: an atom declared in fixy::atom::stdio is missing from "
              "stdio_atom_roster.");

static_assert(every_roster_member_is_atom_<stdio_atom_roster>(),
              "fixy/atoms/Stdio.h: a member of stdio_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<stdio_atom_roster, Axis::Stdio>(),
              "fixy/atoms/Stdio.h: every stdio atom engages Axis::Stdio.");
static_assert(every_roster_member_lifts_<stdio_atom_roster>(),
              "fixy/atoms/Stdio.h: every stdio atom lifts to an effect row.");

// A write lifts IO and Block, whatever the stream.
static_assert(std::is_same_v<::foundation::effects::lift_row_t<stdio::write<stdio::streams::Stderr>>,
                             ::foundation::effects::Row<::foundation::effects::Effect::IO,
                                                        ::foundation::effects::Effect::Block>>);
static_assert(std::is_same_v<::foundation::effects::lift_row_t<stdio::write<stdio::streams::Debug>>,
                             ::foundation::effects::lift_row_t<stdio::write<stdio::streams::Stdout>>>);

// The stream tags are arguments, never atoms, and the stream is part of
// the type.
static_assert(!IsAtom<stdio::streams::Stderr>);
static_assert(!IsAtom<stdio::streams::Stdout>);
static_assert(!IsAtom<stdio::streams::Debug>);
static_assert(!std::is_same_v<stdio::write<stdio::streams::Stderr>, stdio::write<stdio::streams::Stdout>>);
static_assert(!std::is_same_v<stdio::write<stdio::streams::Stdout>, stdio::write<stdio::streams::Debug>>);

}  // namespace fixy::atom::detail::stdio_atom_self_test
