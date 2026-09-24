// A different header can register a new combinator, but not a second
// registration of a shape that has one.  The answer for a shape is read
// once in a translation unit, so the second registration of Send below
// cannot change the answers for Send that are already read.  The next
// read of any other shape counts the registrations of every shape, and
// the second registration of Send stops the build there.

#include <fixy/session/Protocol.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator send_again{
    .shape = ^^::fixy::session::Send,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::fixy::session::Recv,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
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
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

int main() { return ::fixy::session::is_well_formed_v<Emit<int, ::fixy::session::End>> ? 0 : 1; }
