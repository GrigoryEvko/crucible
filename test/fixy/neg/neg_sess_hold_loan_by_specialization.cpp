// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes PermHold and takes the read loan out of
// a Borrowed from the specialization.  The loan markers befriend only
// HoldFactory, so the specialization gets no access.  With access, the
// loan would reach a reader that no set records as a borrower.  The unit
// names the Borrowed through the lend of a hold.
//
// Expected diagnostic: the loan of the Borrowed is private in this
// context.
#include <fixy/session/Payload.h>

#include <utility>

namespace neg_sess_hold_loan_by_specialization_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct ForgeTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_hold_loan_by_specialization_types

namespace fixy::session {
template <>
class PermHold<::foundation::permissions::PermSet<neg_sess_hold_loan_by_specialization_types::ForgeTag>> {
public:
    template <class Message>
    static auto steal(Message& message) {
        return std::move(message.loan_);
    }
};
}  // namespace fixy::session

namespace neg_sess_hold_loan_by_specialization_types {

namespace fs = ::fixy::session;
namespace fp = ::foundation::permissions;

// The message that a hold of Region lends.
using Lent = decltype(std::declval<fs::PermHold<fp::PermSet<Region>>>().template lend<Region>(0).first);

// The deduced return type instantiates the body of steal.
using Stolen = decltype(fs::PermHold<fp::PermSet<ForgeTag>>::steal(std::declval<Lent&>()));

}  // namespace neg_sess_hold_loan_by_specialization_types

int main() { return 0; }
