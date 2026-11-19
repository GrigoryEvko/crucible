// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The home of a registration is the file of its shape, and the home of a
// class template is the file of its first declaration
// (fixy/session/Registry.h).  This file declares Send before it includes
// fixy/session/Protocol.h, so the first declaration of Send is here, and
// the registration of Send in fixy/session/Protocol.h stands outside the
// home.  A forward declaration cannot give a shape a second home, so the
// first query of Send stops the build.
//
// Expected diagnostic: a registration stands outside the file of its
// shape.

#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
template <typename T, typename Rest>
struct Send;
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include <fixy/session/Protocol.h>

int main() { return ::fixy::session::is_well_formed_v<::fixy::session::Send<int, ::fixy::session::End>> ? 0 : 1; }
