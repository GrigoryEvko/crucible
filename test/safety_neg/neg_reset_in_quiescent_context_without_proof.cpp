// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture for fixy-A1-032: OneShotFlag::
// reset_in_quiescent_context demands a `QuiescenceProof` passkey.
// This fixture pins the gate by attempting the rename's pre-fix
// call shape — `flag.reset_in_quiescent_context()` (no argument).
// The compiler MUST reject because the member function takes a
// `QuiescenceProof` parameter; the legacy `reset_unsafe()` no-arg
// surface is GONE.
//
// Why the gate matters (CLAUDE.md §II.6 ThreadSafe):
//
//   `reset_in_quiescent_context` issues a `memory_order_relaxed`
//   store on a cross-thread atomic.  The relaxed order is sound
//   ONLY when both producer (signal()) and consumer
//   (check_and_run() / peek_nothrow()) are quiescent — typically
//   at shutdown, after `signal()` has been observed by every
//   consumer, or between iterations after `join()` on the bg
//   thread.  Calling it during steady-state operation reorders
//   unpredictably under TSan and produces lost-signal /
//   double-signal hazards.
//
//   The legacy surface (`reset_unsafe()`) made the precondition
//   discoverable only by chasing the `_unsafe` suffix back to the
//   header.  fixy-A1-032 lifts the precondition to the type
//   system: every call site must explicitly construct a
//   `OneShotFlag::QuiescenceProof{}` value to invoke the reset.
//   The explicit construction is the structural audit point —
//   `grep "QuiescenceProof{}"` returns every certified quiescent
//   reset site in the codebase, and a reviewer who sees one in a
//   diff knows immediately "this caller is asserting structural
//   quiescence".
//
//   The passkey's default ctor is `explicit` so:
//
//     flag.reset_in_quiescent_context()        → compile error
//                                                 (no matching
//                                                  function — this
//                                                  fixture).
//     flag.reset_in_quiescent_context({})      → compile error
//                                                 (can't init via
//                                                  `{}` an
//                                                  explicit ctor).
//                                                 (Companion
//                                                  fixture.)
//     flag.reset_in_quiescent_context(
//         OneShotFlag::QuiescenceProof{})      → OK.
//
// Bug class targeted: caller forgetting / refactoring out the
// precondition assertion.  In a refactor that mechanically
// renames `reset_unsafe()` to `reset_in_quiescent_context()`
// without realizing the API also gained a parameter, the call
// site silently becomes a compile error here.  Reviewer learns
// from the diagnostic that the new API requires a passkey —
// they read the header, see the `QuiescenceProof` doc-block,
// and confirm the call site really is at a structural-quiescence
// point before adjusting.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "too few arguments" / "no matching function" /
//   "candidate function not viable" / "expects 1 argument" /
//   "expected.*argument".

#include <crucible/handles/OneShotFlag.h>

namespace {

void violate(crucible::safety::OneShotFlag& flag) {
    // No QuiescenceProof argument.  Pre-fixy-A1-032 (the legacy
    // `reset_unsafe()`) this compiled cleanly; post-fix the
    // compiler MUST reject — the signature is
    // `void reset_in_quiescent_context(QuiescenceProof) noexcept`.
    flag.reset_in_quiescent_context();
}

}  // namespace

int main() { return 0; }
