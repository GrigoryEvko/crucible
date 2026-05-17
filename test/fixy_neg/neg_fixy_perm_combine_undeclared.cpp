// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #4: mint_permission_combine via fixy:: alias
// rejects when `splits_into<In, L, R>` has NOT been specialized true.
//
// Violation: combine is the symmetric inverse of split — the substrate
// requires the SAME `splits_into<In, L, R>::value` trait the split
// minter does.  Pre-mint roots for Left/Right (declaring their own
// trivial splits_into so the roots themselves are valid), then attempt
// to combine into a parent tag for which NO splits_into specialization
// has been declared.  Routing through `fixy::perm::mint_permission_combine`
// must reject identically to the substrate.
//
// Expected diagnostic: "splits_into" — the static_assert message names
// the missing trait specialization on the parent tag.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_combine_undeclared {

struct Parent {};  // no splits_into specialization declared
struct Left {};
struct Right {};

}  // namespace neg_fixy_perm_combine_undeclared

int main() {
    namespace tags  = neg_fixy_perm_combine_undeclared;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto l = fperm::mint_permission_root<tags::Left>();
    auto r = fperm::mint_permission_root<tags::Right>();
    auto whole = fperm::mint_permission_combine<tags::Parent>(
        std::move(l), std::move(r));
    safe::permission_drop(std::move(whole));
    return 0;
}
