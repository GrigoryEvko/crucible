// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #3: mint_permission_split via fixy:: alias
// rejects when `splits_into<In, L, R>` has NOT been specialized true.
//
// Violation: `mint_permission_split<L, R>(Permission<In>&&)` carries a
// `static_assert(splits_into_v<In, L, R>, ...)` inside the body and
// the body is consteval-eligible — the assert fires at the alias call
// site identically to the substrate call.  Routing through
// `fixy::perm::mint_permission_split` must reject identically.
//
// Expected diagnostic: "splits_into" — the assertion message names the
// missing trait specialization.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_split_undeclared {

struct Whole {};
struct Left {};
struct Right {};

// Note: NO splits_into specialization declared.  The substrate's
// static_assert(splits_into_v<Whole, Left, Right>, ...) must fire.

}  // namespace neg_fixy_perm_split_undeclared

int main() {
    namespace tags  = neg_fixy_perm_split_undeclared;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto [l, r] = fperm::mint_permission_split<tags::Left, tags::Right>(
        std::move(whole));
    safe::permission_drop(std::move(l));
    safe::permission_drop(std::move(r));
    return 0;
}
