// Negative compile test.  This file must fail to compile.
//
// The registry of the session layer is a root of families
// (fixy/session/Registry.h).  A root holds no registration of its own: a
// registration that stands in the root, outside every family, has no
// seal that counts it.  So a read of the root that finds such a
// registration beside a family stops the build.
//
// Expected diagnostic: a root of a registry holds a registration of its
// own.

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
