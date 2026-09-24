// SPDX-License-Identifier: Apache-2.0
//
// Companion negative-compile fixture for fixy-A1-032.  The
// primary fixture (`neg_reset_in_quiescent_context_without_proof`)
// catches the no-argument shape; this companion catches the
// implicit braced-init shape that `explicit QuiescenceProof()`
// must ALSO reject.
//
// Why the explicit-ctor matters: a default-constructible
// `QuiescenceProof` with NON-explicit ctor would permit
//
//     flag.reset_in_quiescent_context({})
//
// which is structurally indistinguishable to a reviewer from any
// other braced-init parameter pass.  The audit-discoverability
// claim (grep "QuiescenceProof{}") requires that the literal
// type-name appear at every call site.  Marking the ctor
// `explicit` closes the `{}` shortcut: callers MUST spell
// `OneShotFlag::QuiescenceProof{}` (or a named local) for the
// gate to fire.  This fixture pins that property.
//
// Distinct from the companion fixture:
//
//   * neg_reset_in_quiescent_context_without_proof (primary) —
//     witness: zero arguments.  Catches refactor-induced loss of
//     the parameter entirely.  The compile error names "too few
//     arguments".
//
//   * neg_reset_in_quiescent_context_braced_init (this fixture) —
//     witness: braced-init `{}`.  Catches the implicit-conversion
//     bypass that would defeat the grep-discoverability audit
//     trail.  The compile error names "explicit constructor" /
//     "explicit conversion" / "cannot convert" depending on the
//     diagnostic dialect — the bouncer is the `explicit` keyword
//     on `QuiescenceProof::QuiescenceProof()`.
//
// Together the pair witnesses both bug shapes: missing-passkey
// (primary) AND implicit-passkey-via-braced-init (this).  The
// invariant the type system preserves is "every reset call site
// spells `OneShotFlag::QuiescenceProof{}` literally" — which is
// exactly what grep can find.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "explicit constructor" / "explicit.*conversion" /
//   "no matching function" / "could not convert" /
//   "cannot convert" / "not a valid template argument".

#include <crucible/handles/OneShotFlag.h>

namespace {

void violate(crucible::safety::OneShotFlag& flag) {
    // Braced-init implicit-conversion attempt.  The `explicit`
    // qualifier on `QuiescenceProof::QuiescenceProof()` forbids
    // the implicit `{} → QuiescenceProof` conversion — fixy-A1-032
    // structurally requires the literal type-name at the call
    // site so `grep "QuiescenceProof{}"` audits every certified
    // reset.
    flag.reset_in_quiescent_context({});
}

}  // namespace

int main() { return 0; }
