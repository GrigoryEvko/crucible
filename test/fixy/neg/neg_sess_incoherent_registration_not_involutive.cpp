// A header can register a combinator in fixy::session::combinators.  A
// registration whose dual does not name it back makes duality lose its
// involution: Push's dual is Pull, and Pull's dual is Shove.  The query
// that first meets Push refuses the registration and says why.

#include <fixy/session/Protocol.h>

namespace {

template <class T, class K>
struct Push {};
template <class T, class K>
struct Pull {};
template <class T, class K>
struct Shove {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator push{
    .shape = ^^::Push,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Pull,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator pull{
    .shape = ^^::Pull,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Shove,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
inline constexpr ::foundation::algebra::transition::combinator shove{
    .shape = ^^::Shove,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Pull,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
}  // namespace fixy::session::combinators

int main() { return sizeof(::fixy::session::dual_of<Push<int, ::fixy::session::End>>) == 0 ? 1 : 0; }
