// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `crucible::safety::mint_tagged<Tag, T>` with a
// SCALAR (non-class) type as Tag.  The factory's requires-clause
// `ValidTaggedTag<Tag>` is defined as `std::is_class_v<Tag>`; scalar
// types (int, char, void*, etc.) fail the concept and GCC rejects
// at constraint-checking time before the body is instantiated.
//
// Discipline rationale (Tagged.h, §XXI Universal Mint Pattern):
//   The phantom-Tag protocol of `Tagged<T, Tag>` carries provenance
//   / trust / access / version semantics ENTIRELY at the type level.
//   The conventional tags in source::* / trust::* / access::* /
//   version::* / vessel_trust::* are ALL empty structs because the
//   tag itself carries no runtime state — only its TYPE IDENTITY
//   matters for overload resolution and concept gating.
//
//   Allowing scalar tags would let `Tagged<int, int>` compile, which
//   is semantically nonsense — you cannot interpret `int` as a
//   provenance / trust / access category.  The ValidTaggedTag
//   concept catches this as a type-shape category error at the §XXI
//   mint authorization point, BEFORE any Tagged<T, int> can leak
//   out of the construction site and pollute downstream APIs.
//
// HS14 — paired with neg_tagged_unrelated_source_mismatch for distinct
// mismatch classes:
//   * Class U (THIS file):    concept-gate rejection at constraint-checking
//   * Class T (sibling):      typed-overload rejection across distinct tag types
// Together the pair pins both soundness layers of Tagged's discipline:
//   (a) tag-shape gate (class-type required); and
//   (b) tag-identity gate (different tags = different types, no
//       implicit conversion).
//
// U-142 — first neg-compile pair for safety::Tagged<> (closes the
// Tagged slice of backlog #146 A8-P2 alongside U-140's Machine and
// U-141's ConstantTime coverage).

#include <crucible/safety/Tagged.h>

// Anchor a legitimate call so the file is self-contained — empty-
// struct tag satisfies std::is_class_v.  This call compiles.
namespace {
    struct ValidPhantomTag {};
}

[[maybe_unused]] static auto anchor_mint_with_class_tag() {
    return ::crucible::safety::mint_tagged<ValidPhantomTag, int>(42);
}

// VIOLATION: int is a scalar type, NOT a class type.  The requires-
// clause `ValidTaggedTag<Tag>` (≡ `std::is_class_v<Tag>`) is
// unsatisfied.  GCC rejects the template instantiation with
// "constraints not satisfied" naming the `ValidTaggedTag` concept
// and the `std::is_class_v` predicate.
[[maybe_unused]] static auto offending_mint_with_scalar_tag() {
    return ::crucible::safety::mint_tagged<int, int>(42);  // ERROR: scalar tag
}

int main() { return 0; }
