// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Each family of a root of registrations holds one seal for each kind of
// registration that it holds (fixy/session/Registry.h).  The seal states the
// count of the registrations of its kind, so two seals of one kind would
// state two counts, and no read could tell which one holds.  So the first
// read of a family with two seals of one kind stops the build.
//
// Expected diagnostic: a kind of registration has two seals.

#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

namespace {
struct Halt {};
}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators::sealed_twice {
inline constexpr ::fixy::session::transition::combinator halt{
    .shape = ^^::Halt, .kind = ::fixy::session::transition::shape_kind::terminal, .dual = ^^::Halt};
inline constexpr ::fixy::session::transition::seal first_seal{.kind = ^^::fixy::session::transition::combinator,
                                                              .count = 1};
inline constexpr ::fixy::session::transition::seal second_seal{.kind = ^^::fixy::session::transition::combinator,
                                                               .count = 1};
}  // namespace fixy::session::combinators::sealed_twice
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_well_formed_v<Halt> ? 0 : 1; }
