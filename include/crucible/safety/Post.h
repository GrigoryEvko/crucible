// SPDX-License-Identifier: Apache-2.0
// crucible — safety/Post.h
//
// CRUCIBLE_POST — postcondition macro that fires at consteval AND
// runtime, regardless of return-type shape.
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS HEADER EXISTS
// ───────────────────────────────────────────────────────────────────
// Sister header to `safety/Pre.h`.  Same GCC 16.1.1 consteval-bypass
// limitation applied to `post(r: ...)` clauses on the un-patched
// distro build (6 of 7 shapes bypassed; only literal-pointer-comparison
// fired).  Closed on the patched build (PR c++/124241 cherry-pick;
// see misc/08_05_2026_harness.md §0) — but the patched build introduces
// the §13.6 foldable-body always-true post regression that CRUCIBLE_POST
// is immune to.  Shipped as defense-in-depth across both builds.
// See `Pre.h` for the detailed rationale, probe results, and the
// patched-vs-distro behavior matrix.
//
// CRUCIBLE_POST is the macro-based replacement that:
//   * fires at consteval for every return shape (witnessed via
//     `__builtin_trap()`, which is non-constexpr and poisons the
//     surrounding consteval call)
//   * fires at runtime in debug/test builds (via the same Pre.h
//     CRUCIBLE_PRE plumbing — std::abort()-on-violation)
//   * is zero-cost in production (NDEBUG): emits only [[assume(cond)]]
//   * works on any return shape — scalar, struct value, struct ref,
//     pointer, member access, method call — because the macro lives in
//     the function BODY just before the return, not the parser-special
//     post-clause position
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
// Form: `CRUCIBLE_POST(retvar, predicate)` — checks a postcondition
// predicate over an already-computed return value named `retvar`.
//
//   constexpr T fn(int x) noexcept {
//       T r = compute(x);
//       CRUCIBLE_POST(r, post_invariant(r));
//       return r;
//   }
//
// We require the caller to name the return variable explicitly because
// P2900's `post (r: ...)` syntax — which introduces an implicit binding
// — has no equivalent in plain C++.  Naming the variable matches the
// idiom of the existing CLAUDE.md §XII assertion triad.
//
// ───────────────────────────────────────────────────────────────────
// COST MODEL
// ───────────────────────────────────────────────────────────────────
// Identical to CRUCIBLE_PRE — see `safety/Pre.h`.  CRUCIBLE_POST is a
// thin syntactic wrapper that forwards to CRUCIBLE_PRE after a (void)-
// cast on the named return variable to suppress unused-variable
// diagnostics if the predicate happens to not reference `retvar`.
//
// ───────────────────────────────────────────────────────────────────
// SEMANTIC NOTE
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE_POST does NOT add semantic content over CRUCIBLE_PRE — both
// macros expand to the same consteval-aware check + [[assume]] hint.
// The point of the separate spelling is conventional: a reader scanning
// the function body sees `CRUCIBLE_PRE` near the top (input contract)
// and `CRUCIBLE_POST` near the return (output contract).  This matches
// the visual structure that P2900 pre/post clauses provide and helps
// reviewers spot missing post-checks the same way they spot missing
// pre-checks.
//
// ───────────────────────────────────────────────────────────────────
// VC DISCHARGE FRAMING (see Pre.h for full version)
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE_POST is the postcondition half of the dual-side discipline
// codified in feedback_pre_post_dual_discipline.md: every CONTRACT-*
// migration audits BOTH CRUCIBLE_PRE and CRUCIBLE_POST.  Skip post
// only with documented rationale:
//
//   * tautological — body IS the post (e.g. accessor that returns
//     `entries[id]`; post `result == entries[id]` is restatement)
//   * racy — atomic CAS succeeds; re-reading the atomic for the post
//     opens a TOCTOU race window
//   * structurally-not-guaranteed — corner case (e.g. XOR-collision
//     on two-input hash) makes the invariant statistically rare
//     but not provable
//
// Three classes of post seen across the seven migrated headers:
//
//   1. State-mutation: `state == new_value` after a setter.  Catches
//      dropped assignments.  Examples: set_variant, register_external,
//      advance_head, register_op.
//   2. Result-shape: returned value satisfies a structural invariant.
//      Catches dropped guards.  Examples: output_ptr returns nullptr
//      || sid.is_valid(); add_branch returns 2-arm BranchNode;
//      try_append returns MetaIndex::none() || result.raw() < CAPACITY.
//   3. Lifecycle reset: ctor/init/clear/destroy returns the structure
//      to a documented invariant.  Multi-field: each field gets one
//      post.  Examples: KernelCache ctor (3 posts), PoolAllocator
//      destroy (5 posts), IterDet reset (6 posts), CKernel clear (2),
//      Tx begin (3 posts), Arena ctor (4 posts).
//
// Deref-safe disjunction trap: posts where the consequent
// dereferences a witnessed non-null pointer must use C++ short-
// circuit `||`, not `decide::implies` (which evaluates both args
// eagerly).  See feedback_decide_implies_eager_eval.md for the UBSan-
// caught regression and the fix pattern (Tx::activate, 9a0fc58).
//
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE AXIOMS
// ───────────────────────────────────────────────────────────────────
// Same as Pre.h — see that header for the per-axiom analysis.

#pragma once

#include <crucible/safety/Pre.h>   // CRUCIBLE_PRE — the actual check
                                   // (also brings crucible::detail::contract_failed
                                   // declaration for the runtime path)

// ─── CRUCIBLE_POST ─────────────────────────────────────────────────
//
// Form: `CRUCIBLE_POST(retvar, predicate)` — checks `predicate` after
// the function body has produced `retvar`.  Same cost model and
// consteval-fire semantics as CRUCIBLE_PRE.  The `(void)retvar` is a
// belt-and-braces unused-variable suppression for predicates that
// reference `retvar` only through a member or method call.

#define CRUCIBLE_POST(retvar, cond)                                     \
    do {                                                                \
        (void)(retvar);                                                 \
        CRUCIBLE_PRE(cond);                                             \
    } while (0)

// ─── CRUCIBLE_POST_FAST ────────────────────────────────────────────
//
// Symmetric to CRUCIBLE_PRE_FAST: skips the diagnostic-emitting
// `crucible::detail::contract_failed` shim and traps directly on
// violation.  Use on hot return paths where the ~µs of stderr-flush
// + breakpoint-hook overhead matters.  Diagnostic loss is acceptable;
// production debugging falls back to the core dump's stack trace.
#define CRUCIBLE_POST_FAST(retvar, cond)                                \
    do {                                                                \
        (void)(retvar);                                                 \
        CRUCIBLE_PRE_FAST(cond);                                        \
    } while (0)

// ─── CRUCIBLE_POST_MSG ─────────────────────────────────────────────
//
// Annotated post variant — adds a "note: <msg>" diagnostic line on
// violation.  Use when the postcondition predicate alone is cryptic:
//
//   CRUCIBLE_POST_MSG(r, r.lo != 0,
//                     "compute_storage_nbytes must never return zero "
//                     "for a well-formed TensorMeta");
//
// At consteval the message is unused; the macro behaves identically
// to CRUCIBLE_POST for static_assert-fired neg-compile fixtures.
#define CRUCIBLE_POST_MSG(retvar, cond, msg)                            \
    do {                                                                \
        (void)(retvar);                                                 \
        CRUCIBLE_PRE_MSG(cond, msg);                                    \
    } while (0)
