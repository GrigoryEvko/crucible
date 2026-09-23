#pragma once

// The session atoms: what a binding holds of a live session.  Every atom
// here engages Axis::Protocol.
//
// atom::protocol<Proto> says that a binding speaks a protocol.
// session::live_handle<Proto> says more: the frame of the binding holds a
// handle that still owes the protocol Proto.  The difference is what
// collision rule R004 reads.  A suspension turns the frame into a
// continuation, and a continuation that is dropped or resumed twice
// drops the protocol or does one step two times (Tang, Hillerström,
// Lindley and Morris, POPL 2024).  R004 refuses that frame unless the
// frame is linear.
//
// A binding whose payload is itself a session handle does not need this
// atom.  fixy/Collision.h reads that case from the payload type.  The
// atom is for a frame that holds a handle apart from its payload, for
// example a coroutine that captured one.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::session {

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct live_handle final : atom_of<Axis::Protocol> {};

}  // namespace fixy::atom::session

namespace fixy::atom::detail {

struct session_atom_witness_proto final {};

using session_atom_roster = std::tuple<session::live_handle<session_atom_witness_proto>>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::session_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::session, session_atom_roster>(),
              "fixy/atoms/Session.h: an atom declared in fixy::atom::session is missing from "
              "session_atom_roster.");

static_assert(every_roster_member_is_atom_<session_atom_roster>(),
              "fixy/atoms/Session.h: a member of session_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<session_atom_roster, Axis::Protocol>(),
              "fixy/atoms/Session.h: every session atom engages Axis::Protocol.");

// The protocol is part of the type, and the atom is a different grade
// from atom::protocol on the same protocol.
struct other_proto final {};
static_assert(!std::is_same_v<session::live_handle<session_atom_witness_proto>, session::live_handle<other_proto>>);
static_assert(!std::is_same_v<session::live_handle<other_proto>, ::fixy::atom::protocol<other_proto>>);

}  // namespace fixy::atom::detail::session_atom_self_test
