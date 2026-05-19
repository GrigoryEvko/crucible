// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A4-030 negative fixture #1 (HS14 ≥2 floor): substrate-vs-fixy
// disjointness discriminator route — `HotPathViolation` substrate tag.
//
// FOUND-E01 + FIXY-AUDIT-C8 reconciliation requires that the substrate
// `safety::diag::Catalog` (HotPathViolation, EffectRowMismatch,
// CapBypass, ResidencyDrift, ...) and `FixyCatalog`
// (FixyNotEngaged_Type, FixyNotEngaged_Refinement, ...) be DISJOINT.
// A substrate tag must NEVER register as a fixy diagnostic and vice
// versa.
//
// fixy-A4-030 strengthened the prior hand-picked spot-check pair into
// a full `safety::diag::catalog_size`-walking `std::index_sequence`
// fold (Reject.h:531-544) that asserts `!is_fixy_diag_v<...>` for
// every substrate Catalog entry.  This fixture witnesses the
// discriminator from the user side: a hand-written
// `static_assert(is_fixy_diag_v<HotPathViolation>)` claims (wrongly)
// that the substrate Tier-1 hot-path-violation tag IS a fixy
// diagnostic.  Because the substrate Catalog and FixyCatalog are
// disjoint by design, `in_tuple_impl<HotPathViolation, FixyCatalog>`
// folds to `false` and the user-side assertion fires.
//
// The two HS14 fixtures (this file + neg_fixy_reject_effectrow_
// misclassified.cpp) exercise the discriminator on two semantically
// orthogonal Catalog entries.  HotPathViolation lives in the
// hot-path-tier axis (Tier-1 substrate diagnostic emitted by
// HotPath<> wrapper composition); EffectRowMismatch lives in the
// Met(X) effect-row axis (Tier-0 substrate diagnostic emitted by
// Subrow<>/row_contains<> failures).  Distinct mismatch classes —
// the two static_asserts witness the SAME structural invariant
// (disjointness) on DIFFERENT semantic axes, ruling out an axis-
// specific carve-out.
//
// Expected diagnostic: "static assertion failed" referring to the
// `is_fixy_diag_v<HotPathViolation>` predicate, OR `static_assert`
// pointing at `HotPathViolation`.

#include <crucible/fixy/Reject.h>
#include <crucible/safety/Diagnostic.h>

int main() {
    // Substrate tag — MUST NOT register as a fixy diagnostic.  The
    // disjointness fold in Reject.h proves this for the catalog at
    // large; here we witness the discriminator from the user side.
    static_assert(::crucible::fixy::is_fixy_diag_v<
                      ::crucible::safety::diag::HotPathViolation>,
        "fixy-A4-030 HS14 fixture #1: HotPathViolation is a substrate "
        "Catalog entry, not a FixyCatalog entry; the discriminator "
        "MUST reject it.  If you see this message during a compile "
        "audit, the FixyCatalog vs safety::diag::Catalog disjointness "
        "invariant has been broken.");

    return 0;
}
