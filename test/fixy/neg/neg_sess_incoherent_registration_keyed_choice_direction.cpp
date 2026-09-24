// An output step names an input choice as its keyed choice.  A keyed step
// stands for a choice of its own direction, so a send would stand for an
// Offer.  The query that first meets the registration refuses it.

#include <fixy/session/Subtype.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator backwards_emit{
    .shape = ^^::Emit,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::foundation::algebra::transition::variance::covariant,
    .keyed_choice = ^^::fixy::session::Offer};
inline constexpr ::foundation::algebra::transition::combinator backwards_absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant,
    .keyed_choice = ^^::fixy::session::Select};
}  // namespace fixy::session::combinators

int main() {
    using Plain = Emit<int, ::fixy::session::End>;
    return ::fixy::session::is_subtype_sync_v<Plain, Plain> ? 0 : 1;
}
