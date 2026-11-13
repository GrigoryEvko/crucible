// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a set with a region lent out holds no open
// loan, so that a handle closes and a hold ends while the loan is out.
// The query is a concept over the set, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Flow.h>
#include <foundation/Quarantine.h>

namespace neg_sess_seal_perm_set_has_open_loan_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Lent = ::foundation::permissions::PermSet<::fixy::session::LentOut<Region>>;
}  // namespace neg_sess_seal_perm_set_has_open_loan_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
template <>
inline constexpr bool perm_set_has_open_loan_v<neg_sess_seal_perm_set_has_open_loan_types::Lent> = false;
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
