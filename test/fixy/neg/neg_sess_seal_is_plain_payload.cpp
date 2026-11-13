// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit marks a payload that moves a token as plain, so that
// a checkpoint session admits it.  The predicate is a concept over the
// facts of the payload walk, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Flow.h>
#include <foundation/Quarantine.h>

namespace neg_sess_seal_is_plain_payload_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Moves = ::fixy::session::Transferable<int, Region>;
}  // namespace neg_sess_seal_is_plain_payload_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
template <>
inline constexpr bool is_plain_payload_v<neg_sess_seal_is_plain_payload_types::Moves> = true;
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
