// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The home of the registration of a class is the file of its definition
// (fixy/session/Registry.h).  A class that is only declared has no
// definition in the unit, so a unit that sees a definition in a different
// header could read a different registration.  The first query of the
// shape stops the build.
//
// Expected diagnostic: a registered class has no definition here.

#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

namespace {
struct Halt;
}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators::declared {
inline constexpr ::fixy::session::transition::combinator halt{
    .shape = ^^::Halt, .kind = ::fixy::session::transition::shape_kind::terminal, .dual = ^^::Halt};
inline constexpr ::fixy::session::transition::seal combinator_seal{.kind = ^^::fixy::session::transition::combinator,
                                                                   .count = 1};
}  // namespace fixy::session::combinators::declared
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_well_formed_v<Halt> ? 0 : 1; }
