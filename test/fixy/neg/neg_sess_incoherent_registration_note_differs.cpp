// A choice pair whose two sides name different note templates.  The dual
// of a choice with a Sender note cannot carry a Via note, so the dual
// drops the note, and duality loses its involution.  The query that
// first meets the pair refuses the registration and says why.

#include <fixy/session/Protocol.h>

namespace {

template <class... Bs>
struct Ask {};
template <class... Bs>
struct Answer {};
template <class R>
struct Via {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator ask{
    .shape = ^^::Ask,
    .kind = ::foundation::algebra::transition::shape_kind::choice,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Answer,
    .annotation = ^^::Via};
inline constexpr ::foundation::algebra::transition::combinator answer{
    .shape = ^^::Answer,
    .kind = ::foundation::algebra::transition::shape_kind::choice,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Ask,
    .annotation = ^^::fixy::session::Sender};
}  // namespace fixy::session::combinators

struct Bob {};

int main() {
    return sizeof(::fixy::session::dual_of<Ask<Via<Bob>, ::fixy::session::End>>) == 0 ? 1 : 0;
}
