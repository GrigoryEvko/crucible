// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::mint_read_view<Tag> (fixy-A1-019).
//
// Premise: mint_read_view<Tag>(Permission<Tag> const&) is the §XXI
// chokepoint factory for ReadView<Tag>.  It takes a Permission token
// by const-ref; passing a non-Permission value (an int, a raw pointer,
// a random struct) must be rejected at compile time so the type
// system carries the "ReadView authority derives from a parent
// Permission" obligation.
//
// Distinct mismatch class from companion fixture
// neg_mint_read_view_explicit_tag_mismatch.cpp:
//   * This fixture: PARAMETER-SHAPE gate (deduction sees no
//                   Permission<Tag>; the template never instantiates).
//   * Companion:    TAG-IDENTITY gate (explicit Tag clashes with the
//                   Tag deduced from the argument's Permission).

#include <crucible/permissions/ReadView.h>

int main() {
    // 42 is not a Permission<Tag>; template arg deduction fails.
    auto v = crucible::safety::mint_read_view(42);
    (void)v;
    return 0;
}
