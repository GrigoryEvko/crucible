// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// One parked token has one loan.  A copy of the loan would be a second
// read right that the parked token does not wait for, so a reader could
// keep one copy after the other copy ended the loan.  The copy
// constructor of ReadLoan is deleted.
//
// Expected diagnostic: use of the deleted copy constructor of ReadLoan.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto loan = ::foundation::permissions::mint_read_loan(::foundation::permissions::mint_permission_root<Region>());
    auto copy = loan.first;
    (void)copy;
    return 0;
}
