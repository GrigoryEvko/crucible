// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A1-005 (#1547):
// mint_tagged<Tag, T>(value) ValidTaggedTag<Tag> rejection,
// pointer-tag branch.
//
// Premise: same as companion fixture
// neg_mint_tagged_scalar_tag_rejected.cpp.  Pointer types satisfy
// neither std::is_class_v<T> nor any phantom-tag convention in
// Tagged.h — they are a distinct std::is_* category from scalars
// (different trait family) and merit a separate compile-time
// witness so the concept gate's coverage cannot silently regress
// to "scalars only".
//
// Distinct mismatch class from companion:
//   * Companion:    scalar tag (int)
//   * This fixture: pointer tag (int*)
// Both ValidTaggedTag<Tag> failure modes documented; both must
// fire to demonstrate the gate is not accidentally permissive.

#include <crucible/safety/Tagged.h>

int main() {
    using crucible::safety::mint_tagged;

    // Should FAIL: Tag = int* is not a class type —
    // ValidTaggedTag<int*> is false, concept gate rejects the mint.
    auto t = mint_tagged<int*, int>(42);
    (void)t;
    return 0;
}
