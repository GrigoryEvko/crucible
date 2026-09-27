// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that an endpoint which holds a region can
// receive a second token of it, so that one set holds two owners of the
// region.  The predicate is a concept over the payload walk, and a
// concept has no explicit specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Handle.h>

namespace neg_sess_seal_handle_admits_recv_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Moves = ::fixy::session::Transferable<int, Region>;
}  // namespace neg_sess_seal_handle_admits_recv_types

namespace fixy::session::detail {
template <>
inline constexpr bool handle_admits_recv_v<::foundation::permissions::PermSet<neg_sess_seal_handle_admits_recv_types::Region>,
                                           neg_sess_seal_handle_admits_recv_types::Moves> = true;
}  // namespace fixy::session::detail

int main() { return 0; }
