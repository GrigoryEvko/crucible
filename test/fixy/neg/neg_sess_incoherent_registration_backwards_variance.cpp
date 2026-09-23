// An output step must be covariant in its payload, and an input step
// contravariant.  This pair flips its variance under duality, so the
// flip rule alone admits it, but each side has the variance of the other
// direction.  Refinement then lets a sender put a bare int on the wire
// where the peer, which speaks the dual of the supertype, receives a
// positive int.  The query that first meets the registration refuses it.

#include <fixy/Refined.h>
#include <fixy/session/Subtype.h>

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
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
inline constexpr ::foundation::algebra::transition::combinator absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
}  // namespace fixy::session::combinators

int main() {
    using Bare = Emit<int, ::fixy::session::End>;
    using Positive = Emit<::fixy::Refined<::fixy::positive, int>, ::fixy::session::End>;
    return ::fixy::session::is_subtype_sync_v<Bare, Positive> ? 0 : 1;
}
