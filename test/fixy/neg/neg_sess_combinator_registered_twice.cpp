// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A second registration of a combinator of the session layer, in a family
// of its own, would give Send a second answer in this translation unit.
// The registration of a shape stands in the file of the shape, and Send
// stands in fixy/session/Protocol.h.  So the first query that reads Send
// refuses the registration that this file adds.
//
// Expected diagnostic: a registration stands outside the file of its
// shape.

#include <fixy/session/Protocol.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators::again {
inline constexpr ::fixy::session::transition::combinator send_again{
    .shape = ^^::fixy::session::Send,
    .kind = ::fixy::session::transition::shape_kind::step,
    .direction = ::fixy::session::transition::polarity::output,
    .dual = ^^::fixy::session::Recv,
    .payload_variance = ::fixy::session::transition::variance::invariant};
inline constexpr ::fixy::session::transition::seal combinator_seal{.kind = ^^::fixy::session::transition::combinator,
                                                                   .count = 1};
}  // namespace fixy::session::combinators::again
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return ::fixy::session::is_well_formed_v<::fixy::session::Send<int, ::fixy::session::End>> ? 0 : 1; }
