// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A4-030 negative fixture #2 (HS14 ≥2 floor): substrate-vs-fixy
// disjointness discriminator route — `EffectRowMismatch` substrate
// tag.  Sibling to neg_fixy_reject_hotpath_misclassified.cpp on an
// orthogonal Catalog axis.
//
// EffectRowMismatch is the substrate Tier-0 Met(X) effect-row
// diagnostic, emitted whenever `Subrow<R1, R2>` or `row_contains<E,
// R>` fails at a ctx boundary.  Like every entry in
// `safety::diag::Catalog`, it is structurally distinct from
// FixyCatalog — a hand-written `static_assert(is_fixy_diag_v<
// EffectRowMismatch>)` claims (wrongly) that the substrate's
// effect-row diagnostic IS a fixy diagnostic, and the discriminator
// folds to false.
//
// Why this is a DISTINCT mismatch class from fixture #1:
//   - Fixture #1 exercises HotPathViolation (hot-path tier axis, Tier-1)
//   - Fixture #2 exercises EffectRowMismatch (Met(X) effect-row axis, Tier-0)
//   - The two tags live at different Catalog indices and cover
//     different semantic axes (hot-path residency vs effect-row
//     containment).  Both must reject from FixyCatalog by the same
//     disjointness invariant, but a buggy fold that accidentally
//     collapsed the union on ONE axis (e.g., row-axis-only carve-out)
//     would fail fixture #2 while leaving fixture #1 green.
//
// Pre-A4-030 the disjointness witness was the two hand-picked
// `static_assert(!is_fixy_diag_v<HotPathViolation>)` and
// `static_assert(!is_fixy_diag_v<EffectRowMismatch>)` cells at
// Reject.h:500-505.  Post-A4-030 the witness is the structural fold
// at Reject.h:531-554 that walks every Catalog entry; the
// hand-picked cells remain as fast-firing redundant guards that
// surface a clean diagnostic before the fold's index-sequence
// expansion.  These HS14 fixtures cement BOTH layers — fixing the
// fold without fixing the spot-checks would still leave these
// fixtures green; fixing the spot-checks without fixing the fold
// would still leave these fixtures green; only a regression on the
// DISCRIMINATOR ITSELF turns one or both fixtures red.
//
// Expected diagnostic: "static assertion failed" referring to the
// `is_fixy_diag_v<EffectRowMismatch>` predicate, OR `static_assert`
// pointing at `EffectRowMismatch`.

#include <crucible/fixy/Reject.h>
#include <crucible/safety/Diagnostic.h>

int main() {
    // Substrate tag — MUST NOT register as a fixy diagnostic.  The
    // disjointness fold in Reject.h proves this for the catalog at
    // large; here we witness the discriminator on the effect-row
    // axis specifically.
    static_assert(::crucible::fixy::is_fixy_diag_v<
                      ::crucible::safety::diag::EffectRowMismatch>,
        "fixy-A4-030 HS14 fixture #2: EffectRowMismatch is a "
        "substrate Catalog entry (the Met(X) effect-row diagnostic), "
        "not a FixyCatalog entry; the discriminator MUST reject it. "
        "If you see this message during a compile audit, the "
        "FixyCatalog vs safety::diag::Catalog disjointness invariant "
        "has been broken on the effect-row axis.");

    return 0;
}
