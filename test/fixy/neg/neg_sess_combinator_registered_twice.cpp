// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A second registration of a combinator of the session layer, written
// before fixy/session/Protocol.h, would give Send a second answer in this
// translation unit.  The seal of the registry counts every registration of
// the namespace at the end of that header, so the count differs and the
// header stops the build.
//
// Expected diagnostic: the registry holds a combinator registration that
// its seal does not count.
#include <foundation/algebra/Transition.h>

namespace fixy::session {
template <typename T, typename Rest>
struct Send;
template <typename T, typename Rest>
struct Recv;
}  // namespace fixy::session

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator send_again{
    .shape = ^^::fixy::session::Send,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::fixy::session::Recv,
    .payload_variance = ::foundation::algebra::transition::variance::invariant};
}  // namespace fixy::session::combinators

#include <fixy/session/Protocol.h>

int main() { return 0; }
