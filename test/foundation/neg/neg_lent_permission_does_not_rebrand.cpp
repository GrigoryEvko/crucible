// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A LentPermission holds the token of one instance while its read loan is
// out.  Erasure runs one way only, to the erased brand.  If a parked token
// of instance B took the brand of instance A, the loan of A could bring
// back B's token while the loan of B is still out, and a writer would
// hold a token that a reader still reads.
//
// The parked Permission refuses the rebrand too, but only in the body of
// the constructor.  The constraint of the erasure constructor refuses it
// at overload resolution, so std::is_constructible and every concept see
// the refusal.
//
// Expected diagnostic: no matching constructor of LentPermission, whose
// erasure constructor asks for the erased brand as its target.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace neg_lent_permission_does_not_rebrand {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_lent_permission_does_not_rebrand

int main() {
    namespace fp = ::foundation::permissions;
    using neg_lent_permission_does_not_rebrand::Region;
    auto held_a = fp::mint_permission_root<Region>();
    auto held_b = fp::mint_permission_root<Region>();
    using BrandA = decltype(held_a)::brand_type;
    auto lent_b = fp::mint_read_loan(std::move(held_b));
    fp::LentPermission<Region, BrandA> rebranded{std::move(lent_b.second)};
    (void)rebranded;
    (void)held_a;
    return 0;
}
