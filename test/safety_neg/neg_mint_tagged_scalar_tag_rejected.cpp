// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A1-005 (#1547):
// mint_tagged<Tag, T>(value) ValidTaggedTag<Tag> rejection.
//
// Premise: mint_tagged is the §XXI authorization point for
// Tagged<T, Tag> construction (CLAUDE.md §XXI).  Tag is a phantom-
// type marker by convention always a class type — the source::* /
// trust::* / access::* / version::* / vessel_trust::* namespaces
// in Tagged.h enumerate the conventional empty-struct shapes.
// Wrapping a SCALAR (`int`, `unsigned`, `float`, ...) as the Tag
// slot is a type-shape category error rejected at the concept
// boundary, NOT a wall of SFINAE deep inside Graded.
//
// Distinct mismatch class from companion fixture
// neg_mint_tagged_pointer_tag_rejected.cpp:
//   * This fixture: scalar tag (int)
//   * Companion:    pointer tag (int*)
// Two independent ValidTaggedTag<Tag> failure modes (scalar vs
// pointer) — both must fire to witness the concept covers the
// full non-class-type rejection range.
//
// Substring "ValidTaggedTag" pins the diagnostic — GCC 16 concept-
// failure messages emit the concept name in the "constraint
// requires" line.

#include <crucible/safety/Tagged.h>

int main() {
    using crucible::safety::mint_tagged;

    // Should FAIL: Tag = int is not a class type — ValidTaggedTag<int>
    // is false, concept gate rejects the mint at the call site.
    auto t = mint_tagged<int, int>(42);
    (void)t;
    return 0;
}
