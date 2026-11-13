// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The combinators of the session layer are sealed in
// fixy/session/Protocol.h.  A combinator that a user file registers after
// the seal would give its shape an answer in the translation units that
// see the file and none in the others, and two files could give one shape
// two answers.  So a read of a shape counts the registrations again, and a
// count that differs from the seal stops the build.
//
// Expected diagnostic: a registration stands outside its seal.
#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators {
inline constexpr ::fixy::session::transition::combinator user_emit{
    .shape = ^^::Emit,
    .kind = ::fixy::session::transition::shape_kind::step,
    .direction = ::fixy::session::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::fixy::session::transition::variance::covariant};
inline constexpr ::fixy::session::transition::combinator user_absorb{
    .shape = ^^::Absorb,
    .kind = ::fixy::session::transition::shape_kind::step,
    .direction = ::fixy::session::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::fixy::session::transition::variance::contravariant};
}  // namespace fixy::session::combinators
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_send_v<Emit<int, ::fixy::session::End>> ? 0 : 1; }
