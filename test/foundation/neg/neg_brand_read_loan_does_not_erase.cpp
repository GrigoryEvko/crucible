// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A read loan carries the brand of the parked token.  An erased loan is a
// loan of any region of the tag, and it opens a view of any of them.  No
// conversion drops the brand of a loan, so the loan of one region cannot
// travel as a loan of the erased identity.
//
// Expected diagnostic: no conversion from the branded ReadLoan to the
// erased ReadLoan.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    namespace fp = ::foundation::permissions;
    auto [loan, lent] = fp::mint_read_loan(fp::mint_permission_root<Region>());
    fp::ReadLoan<Region> erased = std::move(loan);
    (void)erased;
    (void)lent;
    return 0;
}
