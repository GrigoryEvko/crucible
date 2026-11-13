// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that an endpoint with an empty set can send a
// token of a region, so that a send moves a region that the sender does
// not hold.  The predicate is a concept over the payload walk, and a
// concept has no explicit specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Handle.h>
#include <foundation/Quarantine.h>

namespace neg_sess_seal_handle_admits_send_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Moves = ::fixy::session::Transferable<int, Region>;
}  // namespace neg_sess_seal_handle_admits_send_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::detail {
template <>
inline constexpr bool
    handle_admits_send_v<::foundation::permissions::EmptyPermSet, neg_sess_seal_handle_admits_send_types::Moves> = true;
}  // namespace fixy::session::detail
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
