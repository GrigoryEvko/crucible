// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a set with a region lent out holds no open
// loan, so that a handle closes and a hold ends while the loan is out.
// The query is a concept over the set, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Payload.h>

namespace neg_sess_seal_perm_set_has_open_loan_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Lent = ::foundation::permissions::PermSet<::fixy::session::LentOut<Region>>;
}  // namespace neg_sess_seal_perm_set_has_open_loan_types

namespace fixy::session {
template <>
inline constexpr bool perm_set_has_open_loan_v<neg_sess_seal_perm_set_has_open_loan_types::Lent> = false;
}  // namespace fixy::session

int main() { return 0; }
