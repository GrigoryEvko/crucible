// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy::perm::mint_grid_permissions
// (FIXY-U-014, mirrors safety_neg/neg_mint_grid_permissions_call_zero
// for the M-arity boundary).
//
// Premise: the §XXI requires-clause `can_split_grid_v<Whole, M, N>`
// folds M>0 into the soundness gate.  Routing through the
// fixy::perm:: alias MUST preserve that gate — calling
// `mint_grid_permissions<Whole, 0, N>(parent)` cannot synthesize a
// degenerate M=0 grid (zero producers is meaningless and would leak
// the parent permission's producer-side child).
//
// Distinct mismatch class from companion fixture
// neg_fixy_perm_mint_grid_permissions_zero_n.cpp:
//   * This fixture: M=0 boundary (producer arity).
//   * Companion:    N=0 boundary (consumer arity).
//
// Expected diagnostic: a requires-clause failure mentioning
// `can_split_grid_v` or `mint_grid_permissions`.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_grid_zero_m {
struct Whole {};
}  // namespace neg_fixy_perm_grid_zero_m

int main() {
    namespace tags  = neg_fixy_perm_grid_zero_m;
    namespace fperm = ::crucible::fixy::perm;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    // M=0 violates the can_split_grid_v fold; the requires-clause
    // rejects this overload at the fixy:: call site.
    auto grid = fperm::mint_grid_permissions<tags::Whole, 0, 3>(
        std::move(whole));
    (void)grid;
    return 0;
}
