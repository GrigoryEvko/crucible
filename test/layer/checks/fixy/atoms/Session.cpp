// The compile-time checks of fixy/atoms/Session.h.

#include <fixy/atoms/Session.h>

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
