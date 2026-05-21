// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copying a `crucible::safety::OwnedRegion<T, Tag>`.  The
// wrapper deletes the copy ctor with a named reason string
// ("OwnedRegion owns a Permission — copy would duplicate the linear
// token").  OwnedRegion holds a `Permission<Tag>` field which IS the
// CSL linearity proof; copying would forge a second proof token from
// thin air, defeating the whole separation-logic discipline.
//
// Discipline rationale (OwnedRegion.h):
//   OwnedRegion is the arena-backed exclusive-region primitive used
//   to thread CSL Permission ownership through batched data
//   structures (parallel_for_views, parallel_reduce_views,
//   parallel_apply_pair).  The Permission<Tag> embedded in
//   OwnedRegion is its identity — duplicate it and you'd have two
//   "owners" of the same arena slab, violating the frame rule.
//
//   Move IS allowed (the move ctor is defaulted) — exclusive
//   ownership transfers cleanly via move semantics.  But copy is
//   NEVER permitted; the deletion fires at every accidental
//   copy-by-value attempt.
//
// HS14 — paired with neg_ownedregion_cross_tag_mismatch for distinct
// mismatch classes:
//   * Class T-copy (THIS file):     deleted-copy with linearity-
//     duplication reason — Permission-token uniqueness.
//   * Class T-cross-tag (sibling):  typed-overload rejection across
//     distinct Tag template parameters — provenance distinctness.
// Together the pair pins both soundness layers of OwnedRegion:
//   (a) the embedded Permission's linearity is preserved
//       (no copy-as-duplicate); and
//   (b) the phantom Tag is load-bearing for overload resolution
//       (regions for different arenas / sub-arenas don't substitute
//       for each other).
//
// U-145 — first neg-compile pair for safety::OwnedRegion (closes
// the OwnedRegion slice of backlog #146 A8-P2 alongside U-140
// Machine, U-141 ConstantTime, U-142 Tagged, U-143 SealedRefined,
// U-144 Pinned/NonMovable).

#include <crucible/safety/OwnedRegion.h>

#include <utility>

namespace {
    // Production-shape tag — a CSL provenance label.  Any empty
    // class type works; downstream code uses tag names like
    // `region::Train`, `region::Validate`, etc.
    struct RegionTag {};
}

// Anchor: move-construct an OwnedRegion.  Move IS allowed —
// exclusive-ownership transfer.  This call compiles.
[[maybe_unused]] static ::crucible::safety::OwnedRegion<int, RegionTag>
anchor_owned_region_move(
    ::crucible::safety::OwnedRegion<int, RegionTag>&& source)
{
    return std::move(source);
}

// VIOLATION: OwnedRegion deletes its copy ctor with a load-bearing
// reason string ("OwnedRegion owns a Permission — copy would
// duplicate the linear token").  Attempting copy-construction
// triggers the deletion.  GCC emits "use of deleted function" with
// the linearity-duplication reason.
[[maybe_unused]] static ::crucible::safety::OwnedRegion<int, RegionTag>
offending_owned_region_copy(
    const ::crucible::safety::OwnedRegion<int, RegionTag>& source)
{
    return ::crucible::safety::OwnedRegion<int, RegionTag>{source};
    // ERROR: copy ctor deleted — Permission linearity protected.
}

int main() { return 0; }
