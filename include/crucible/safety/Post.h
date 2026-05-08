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
// limitation applies to `post(r: ...)` clauses as it does to `pre()`:
// 6 of 7 parameter/return shapes silently bypass at consteval, only
// the literal-pointer-comparison shape fires.  See `Pre.h` for the
// detailed rationale and probe results.
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
// CRUCIBLE AXIOMS
// ───────────────────────────────────────────────────────────────────
// Same as Pre.h — see that header for the per-axiom analysis.

#pragma once

#include <crucible/safety/Pre.h>   // CRUCIBLE_PRE — the actual check

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
