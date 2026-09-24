// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for foundation::permissions::mint_read_loan.
//
// A loan parks the token until the loan comes back.  A token passed by
// name stays with the caller, so the loan would exist beside a live
// token.  The deleted twin refuses the named token with the reason.
//
// A different class from neg_read_loan_effectful_row_without_ctx.cpp
// (fixture 1): there the row gate refuses the region.
//
// Expected diagnostic: use of the deleted twin of mint_read_loan.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto owned = ::foundation::permissions::mint_permission_root<Region>();
    ::foundation::permissions::mint_read_loan(owned);
    return 0;
}
