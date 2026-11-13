// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a handle with a region lent out can close,
// so that the protocol ends while the loan is out.  The predicate is a
// concept over the set, and a concept has no explicit specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Handle.h>
#include <foundation/Quarantine.h>

namespace neg_sess_seal_perm_set_admits_close_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Lent = ::foundation::permissions::PermSet<::fixy::session::LentOut<Region>>;
}  // namespace neg_sess_seal_perm_set_admits_close_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::detail {
template <>
inline constexpr bool perm_set_admits_close_v<neg_sess_seal_perm_set_admits_close_types::Lent> = true;
}  // namespace fixy::session::detail
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
