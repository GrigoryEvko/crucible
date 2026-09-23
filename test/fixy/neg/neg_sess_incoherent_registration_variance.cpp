// A send and its receive must have opposite payload variance, or
// refinement is not closed under duality: a Send that is covariant faces
// a Recv that is covariant too, and the peer then accepts a wider
// payload than the sender promised.  The query that first meets the
// registration refuses it.

#include <fixy/session/Protocol.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator emit{
    .shape = ^^::Emit,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
}  // namespace fixy::session::combinators

int main() { return ::fixy::session::is_well_formed_v<Emit<int, ::fixy::session::End>> ? 0 : 1; }
