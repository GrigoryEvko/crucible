// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing `OwnedRegion<T, TagA>` to a function whose
// signature demands `OwnedRegion<T, TagB>`.  Despite identical
// element type T, the two OwnedRegion specializations are
// UNRELATED types — phantom Tag is a template parameter that
// carries CSL provenance; the type system gives no implicit
// conversion across distinct tags.
//
// Discipline rationale (OwnedRegion.h, Permission.h):
//   `Permission<Tag>` is the CSL ownership token for a particular
//   region named by Tag.  An OwnedRegion<T, Tag> carries a
//   Permission<Tag> as its load-bearing field — the Tag IS the
//   identity of WHICH arena / sub-arena / training cohort the
//   region belongs to.  An API that demands `OwnedRegion<int,
//   TrainBatch>` refuses `OwnedRegion<int, ValidateBatch>` because
//   the Permission<TrainBatch> and Permission<ValidateBatch> are
//   semantically incompatible — they prove ownership of DIFFERENT
//   slabs.  Mixing them would alias unrelated memory regions.
//
//   This is the frame-rule-by-type-system: every Permission<Tag>
//   instance lives in its own type universe; mixing tags is a
//   compile error.  No runtime check is needed because the
//   incompatibility is structural.
//
// HS14 — paired with neg_ownedregion_copy_rejected for distinct
// mismatch classes:
//   * Class T-copy (sibling):       deleted-copy with linearity-
//     duplication reason — Permission-token uniqueness.
//   * Class T-cross-tag (THIS file): typed-overload rejection
//     across distinct Tag template parameters — provenance
//     distinctness.
// Together the pair pins both soundness layers of OwnedRegion:
//   (a) the embedded Permission's linearity is preserved
//       (no copy-as-duplicate); and
//   (b) the phantom Tag is load-bearing for overload resolution
//       (regions for different arenas / sub-arenas don't substitute
//       for each other).
//
// U-145 — Class T-cross-tag fixture (closes OwnedRegion slice of
// #146 A8-P2).

#include <crucible/safety/OwnedRegion.h>

#include <utility>

namespace {
    // Two distinct provenance tags — production-shape names like
    // region::Train vs region::Validate would carry the same
    // semantic distinction.  Both are empty class types.
    struct RegionTagA {};
    struct RegionTagB {};

    // API entry point demanding a specific tag.  In production this
    // would be a Forge / Mimic / Cipher function whose signature
    // names exactly the arena/cohort it operates over.
    [[maybe_unused]] void process_region_a(
        ::crucible::safety::OwnedRegion<int, RegionTagA>&& /*region*/)
    {
        // body irrelevant — call-site type-check is the test.
    }
}

// Anchor: same-tag call compiles cleanly — RegionTagA accepted by
// the RegionTagA-typed parameter.
[[maybe_unused]] static void anchor_same_tag_call(
    ::crucible::safety::OwnedRegion<int, RegionTagA>&& region)
{
    process_region_a(std::move(region));
}

// VIOLATION: OwnedRegion<int, RegionTagA> and OwnedRegion<int,
// RegionTagB> are unrelated template instantiations — even though
// payload type (int) matches.  C++ overload resolution finds no
// implicit conversion across distinct Tag types; GCC rejects with
// "cannot convert ... RegionTagB ... to ... RegionTagA".
[[maybe_unused]] static void offending_cross_tag_call(
    ::crucible::safety::OwnedRegion<int, RegionTagB>&& region)
{
    process_region_a(std::move(region));   // ERROR: TagB ≠ TagA
}

int main() { return 0; }
