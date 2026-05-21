// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling `.into()` on a SealedRefined<Pred, T>.  The
// load-bearing distinguishing property of SealedRefined vs the
// sibling Refined<Pred, T> is the DELIBERATE ABSENCE of the
// destructive rvalue `into()` extractor.  A reviewer who pattern-
// matches against the Refined idiom and writes
// `std::move(sealed).into()` MUST get a clean compile error so the
// invariant-continuity discipline is enforced at the type level,
// not at code-review-time.
//
// Discipline rationale (SealedRefined.h):
//   `Refined<P, T>` permits explicit destructive extraction via
//   `.into() &&` — the act of consuming is grep-discoverable, and
//   acceptable for occasional "drop the wrapper, pass bare T to
//   sanitized-only sink" patterns.
//
//   `SealedRefined<P, T>` is the right primitive when the
//   predicate captures a CONTINUOUS invariant downstream code
//   relies on — extract-mutate-re-wrap loses the invariant
//   transiently between the two checks.  Deleting `.into()` AT
//   THE TYPE LEVEL means no caller can pull the bare value out;
//   the only path to a different value satisfying P is to mint a
//   FRESH SealedRefined directly from a (mutated) input — which
//   re-fires Pred.
//
//   `const Refined<P, T>` is NOT equivalent — Refined's into() is
//   `&&`-qualified, and an unrelated caller can still
//   `std::move(refined).into()` past the const qualifier.  Only
//   structural deletion in SealedRefined closes the hole.
//
// HS14 — paired with neg_sealedrefined_mint_arg_mismatch for distinct
// mismatch classes:
//   * Class T (THIS file):  member-absent name-lookup rejection
//     — the wrapper's distinguishing feature is structurally
//     witnessed.
//   * Class U (sibling):    PredicateInvocableOn concept-gate
//     rejection at the §XXI mint factory.
// Together the pair pins both soundness layers of the
// SealedRefined discipline:
//   (a) structural sealing (no into() escape hatch); and
//   (b) predicate-invocability gate (mint refuses predicates that
//       cannot evaluate on T).
//
// U-143 — first neg-compile pair for safety::SealedRefined<>
// (closes the SealedRefined slice of backlog #146 A8-P2 alongside
// U-140's Machine, U-141's ConstantTime, U-142's Tagged coverage).

#include <crucible/safety/SealedRefined.h>

#include <utility>

// Anchor a legitimate use — value() observation is the ONLY
// supported read path on SealedRefined; this compiles cleanly.
[[maybe_unused]] static int anchor_sealed_value_read() {
    auto sp = ::crucible::safety::mint_sealed_refined<
        ::crucible::safety::positive, int>(7);
    return sp.value();
}

// VIOLATION: .into() does not exist on SealedRefined<Pred, T>.  Name
// lookup fails at the call site: GCC reports "'class
// crucible::safety::SealedRefined<...>' has no member named 'into'".
// Reviewer-pattern from the Refined idiom must NOT compile here.
[[maybe_unused]] static int offending_sealed_into_extract() {
    auto sp = ::crucible::safety::mint_sealed_refined<
        ::crucible::safety::positive, int>(7);
    return std::move(sp).into();  // ERROR: no into() on SealedRefined
}

int main() { return 0; }
