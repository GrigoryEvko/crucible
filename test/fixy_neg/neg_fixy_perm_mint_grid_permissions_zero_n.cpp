// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy::perm::mint_grid_permissions
// (FIXY-U-014, mirrors safety_neg/neg_mint_grid_permissions_call_zero
// for the N-arity boundary).
//
// Premise: same as the M=0 fixture but for the consumer side — the
// `can_split_grid_v` requires-clause must reject N=0 when routed
// through the `fixy::perm::` alias.  A successful compile here would
// mean a fixy:: caller could construct a zero-consumer grid that
// would leave the parent's consumer-side child orphaned.
//
// Distinct mismatch class from companion fixture
// neg_fixy_perm_mint_grid_permissions_zero_m.cpp:
//   * Companion:    M=0 boundary (producer arity).
//   * This fixture: N=0 boundary (consumer arity).
//
// Expected diagnostic: a requires-clause failure mentioning
// `can_split_grid_v` or `mint_grid_permissions`.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_grid_zero_n {
struct Whole {};
}  // namespace neg_fixy_perm_grid_zero_n

int main() {
    namespace tags  = neg_fixy_perm_grid_zero_n;
    namespace fperm = ::crucible::fixy::perm;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    // N=0 violates the can_split_grid_v fold; rejected through the
    // fixy::perm:: call site identically to the substrate symbol.
    auto grid = fperm::mint_grid_permissions<tags::Whole, 2, 0>(
        std::move(whole));
    (void)grid;
    return 0;
}
