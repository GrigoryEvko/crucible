// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit marks a payload that moves a token as plain, so that
// a checkpoint session admits it.  The predicate is a concept over the
// facts of the payload walk, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Payload.h>

namespace neg_sess_seal_is_plain_payload_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Moves = ::fixy::session::Transferable<int, Region>;
}  // namespace neg_sess_seal_is_plain_payload_types

namespace fixy::session {
template <>
inline constexpr bool is_plain_payload_v<neg_sess_seal_is_plain_payload_types::Moves> = true;
}  // namespace fixy::session

int main() { return 0; }
