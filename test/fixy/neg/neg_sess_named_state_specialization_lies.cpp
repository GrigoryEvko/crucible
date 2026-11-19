// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An explicit specialization of Named lies about the protocol of its name:
// its member protocol and its base say Send<long, End>, and the name says
// Send<int, End>.  The fold reads the protocol of a named state from the
// member of its name (fixy/session/Fold.h), so the lie does not change the
// protocol that a walk checks.  A reader that reads the member of the
// state would step to the protocol that no walk checked, so the member
// claims of the node reader of fixy/session/Protocol.h refuse the
// specialization at the first gate.
//
// Expected diagnostic: the node has a nested member or a base that its
// template arguments do not give.

#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

namespace named_lie_types {
struct Rest {
    using protocol = ::fixy::session::Send<int, ::fixy::session::End>;
};
}  // namespace named_lie_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a specialization of a base template, and a check refuses it")
template <>
struct fixy::session::Named<named_lie_types::Rest> : ::fixy::session::Send<long, ::fixy::session::End> {
    using name = named_lie_types::Rest;
    using protocol = ::fixy::session::Send<long, ::fixy::session::End>;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_well_formed_v<::fixy::session::Named<named_lie_types::Rest>> ? 0 : 1; }
