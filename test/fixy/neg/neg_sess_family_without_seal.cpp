// Negative compile test.  This file must fail to compile.
//
// Each family of a root of registrations holds a seal for each kind of
// registration that it holds (fixy/session/Registry.h).  A family with no
// seal would take a registration that a later header adds, and two units
// would read two answers.  So the first read of a family with a
// registration and no seal stops the build.
//
// Expected diagnostic: a family holds a registration with no seal.

#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

namespace {
struct Halt {};
}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators::unsealed {
inline constexpr ::fixy::session::transition::combinator halt{
    .shape = ^^::Halt, .kind = ::fixy::session::transition::shape_kind::terminal, .dual = ^^::Halt};
}  // namespace fixy::session::combinators::unsealed
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_well_formed_v<Halt> ? 0 : 1; }
