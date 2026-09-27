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

namespace neg_sess_seal_handle_admits_send_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Moves = ::fixy::session::Transferable<int, Region>;
}  // namespace neg_sess_seal_handle_admits_send_types

namespace fixy::session::detail {
template <>
inline constexpr bool
    handle_admits_send_v<::foundation::permissions::EmptyPermSet, neg_sess_seal_handle_admits_send_types::Moves> = true;
}  // namespace fixy::session::detail

int main() { return 0; }
