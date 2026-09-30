// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A LentPermission holds the token of one instance while its read loan is
// out, and no conversion changes its brand.  If a parked token of instance
// B took the brand of instance A, the loan of A can end B's parked token
// while the loan of B is out.  A writer can then hold a token of a region
// that a reader reads at the same time.
//
// The refusal is at overload resolution, so std::is_constructible and
// every concept see it.
//
// Expected diagnostic: no matching constructor of LentPermission.  The
// move constructor takes a parked token of the target brand only.

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
