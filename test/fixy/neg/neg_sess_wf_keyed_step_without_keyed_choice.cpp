// A step pair registered with no keyed choice cannot carry a payload that
// names a label.  Outside a choice such a step stands for a keyed choice
// of one branch, and without a keyed choice there is none, so the protocol
// is not well-formed and the mint refuses it.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};
struct Hello {};
struct Wire {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator unkeyed_emit{
    .shape = ^^::Emit,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator unkeyed_absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

namespace s = ::fixy::session;

int main() {
    auto handle = s::mint_session_handle<Emit<s::Labelled<Hello, int>, s::End>, Wire>(Wire{});
    (void)handle;
    return 0;
}
