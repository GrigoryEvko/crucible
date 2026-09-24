// An output step names a keyed choice and its dual names none.  A keyed
// step and its dual must stand for dual choices, or duality would turn a
// Select of one branch into a plain receive.  The query that first meets
// the registration refuses it.

#include <fixy/session/Subtype.h>

namespace {

template <class T, class K>
struct Emit {};
template <class T, class K>
struct Absorb {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator one_sided_emit{
    .shape = ^^::Emit,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Absorb,
    .payload_variance = ::foundation::algebra::transition::variance::covariant,
    .keyed_choice = ^^::fixy::session::Select};
inline constexpr ::foundation::algebra::transition::combinator one_sided_absorb{
    .shape = ^^::Absorb,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Emit,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

int main() {
    using Plain = Absorb<int, ::fixy::session::End>;
    return ::fixy::session::is_subtype_sync_v<Plain, Plain> ? 0 : 1;
}
