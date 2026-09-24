// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy::perm::mint_read_view<Tag>
// (FIXY-U-014, mirrors safety_neg/neg_mint_read_view_non_permission_arg).
//
// Premise: routing the lifetime-bound borrow factory through the
// `fixy::perm::` alias MUST preserve the substrate's parameter-shape
// gate — passing a non-Permission value (here: an `int`) defeats
// template argument deduction so the call expression is ill-formed
// at the fixy:: import site, NOT just at the substrate symbol.  Without
// this fixture, a future rename that broke the using-decl would
// silently route through a different overload set.
//
// Distinct mismatch class from companion fixture
// neg_fixy_perm_mint_read_view_tag_mismatch.cpp:
//   * This fixture: PARAMETER-SHAPE gate (deduction sees no
//                   Permission<Tag>).
//   * Companion:    TAG-IDENTITY gate (explicit Tag clashes with the
//                   argument's Permission<Tag>).
//
// Expected diagnostic: a substitution-failure / deduction-failure
// naming `mint_read_view` or `Permission` from the fixy:: alias path.

#include <crucible/fixy/Perm.h>

int main() {
    namespace fperm = ::crucible::fixy::perm;
    // 42 is not a Permission<Tag>; argument deduction fails at the
    // fixy::perm:: import site identically to the substrate symbol.
    auto v = fperm::mint_read_view(42);
    (void)v;
    return 0;
}
