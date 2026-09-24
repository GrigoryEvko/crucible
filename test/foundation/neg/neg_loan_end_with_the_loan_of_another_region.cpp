// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture 1 of 2 for foundation::permissions::mint_permission_after_loan.
//
// Two roots of one tag are two regions, and each has its own brand.  The
// loan of the second region cannot end the loan of the first, or the
// first token comes back while its loan is still out.  The brands of
// the parked token and of the loan fail to unify.
//
// A different class from neg_loan_end_with_the_loan_of_another_tag.cpp
// (fixture 2): there the tags differ.
//
// Expected diagnostic: no matching function for
// mint_permission_after_loan, with two brands deduced for one parameter.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto [first_loan, first_lent] =
        ::foundation::permissions::mint_read_loan(::foundation::permissions::mint_permission_root<Region>());
    auto [second_loan, second_lent] =
        ::foundation::permissions::mint_read_loan(::foundation::permissions::mint_permission_root<Region>());
    auto token = ::foundation::permissions::mint_permission_after_loan(std::move(first_lent), std::move(second_loan));
    (void)token;
    (void)first_loan;
    (void)second_lent;
    return 0;
}
