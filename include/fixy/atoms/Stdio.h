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
struct write final : lifting_atom_of<Axis::Stdio, ::foundation::effects::Row<::foundation::effects::Effect::IO,
                                                                             ::foundation::effects::Effect::Block>> {};

}  // namespace fixy::atom::stdio

namespace fixy::atom::detail {

using stdio_atom_roster = std::tuple<stdio::write<stdio::streams::Stderr>, stdio::write<stdio::streams::Stdout>,
                                     stdio::write<stdio::streams::Debug>>;

}  // namespace fixy::atom::detail
