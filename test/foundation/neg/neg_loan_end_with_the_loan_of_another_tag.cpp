// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 2 of 2 for foundation::permissions::mint_permission_after_loan.
//
// A loan of one tag says nothing about a region of another tag, so it
// cannot end the loan of that region.  The tags of the parked token and
// of the loan fail to unify.
//
// A different class from neg_loan_end_with_the_loan_of_another_region.cpp
// (fixture 1): there the tag is the same and the brands differ.
//
// Expected diagnostic: no matching function for
// mint_permission_after_loan, with two tags deduced for one parameter.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

struct Other {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    namespace fp = ::foundation::permissions;
    auto region_loan = fp::mint_read_loan(fp::Permission<Region>{fp::mint_permission_root<Region>()});
    auto other_loan = fp::mint_read_loan(fp::Permission<Other>{fp::mint_permission_root<Other>()});
    auto token = fp::mint_permission_after_loan(std::move(region_loan.second), std::move(other_loan.first));
    (void)token;
    return 0;
}
