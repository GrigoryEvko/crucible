// Negative-compile test. This file must fail to compile.
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

// The tokens are singleton roots, so each tag has a stable identity and
// external linkage.
namespace neg_loan_end_with_the_loan_of_another_tag_types {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

struct Other {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_loan_end_with_the_loan_of_another_tag_types

using namespace neg_loan_end_with_the_loan_of_another_tag_types;

int main() {
    namespace fp = ::foundation::permissions;
    // The two singleton roots share the erased identity, so only the tags
    // differ.
    auto [region_loan, region_lent] = fp::mint_read_loan(fp::mint_singleton_root<Region>());
    auto [other_loan, other_lent] = fp::mint_read_loan(fp::mint_singleton_root<Other>());
    auto token = fp::mint_permission_after_loan(std::move(region_lent), std::move(other_loan));
    (void)token;
    return 0;
}
