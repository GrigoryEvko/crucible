// SPDX-License-Identifier: Apache-2.0
// crucible — safety/Pre.h
//
// CRUCIBLE_PRE — precondition macro that fires at consteval AND runtime,
// regardless of parameter shape.
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS HEADER EXISTS
// ───────────────────────────────────────────────────────────────────
// GCC 16.1.1 implements P2900R14 contracts but its consteval evaluator
// silently bypasses `pre()` clauses for almost every parameter shape.
// Empirical probe of 7 distinct param/return shapes (scalar by-value,
// struct const-ref, struct const-value, struct by-value, struct
// const-pointer, scalar return, struct return field) shows that 6 of 7
// are silently bypassed at consteval; only `pre (p != nullptr)` where
// the predicate compares the parameter directly to nullptr fires.
//
// This is not a regression — it's the current GCC 16 behaviour and is
// not on any release-notes fix list (see GCC 16 release notes in repo
// memory).  Until GCC 17 (or whenever the consteval evaluator catches
// up), every neg-compile fixture that depends on `pre()` firing at
// consteval is silently green when it should be red — a soundness gap
// that surfaces only as missing test coverage.
//
// CRUCIBLE_PRE is a macro-based replacement that:
//   * fires at consteval for every parameter shape (witnessed via
//     `__builtin_trap()`, which is non-constexpr and poisons the
//     surrounding consteval call into "non-constant condition")
//   * fires at runtime in debug/test builds (calls `std::abort()` to
//     produce SIGABRT, the project-canonical contract-violation behavior)
//   * is zero-cost in production (NDEBUG): emits only `[[assume(cond)]]`
//     so the optimizer propagates the invariant without runtime check
//   * works on any parameter shape — scalar, struct ref, struct value,
//     pointer, member access, method call, free function — because the
//     macro lives in the function BODY, not the parser-special pre-clause
//     position
//
// For the postcondition counterpart see `safety/Post.h`.
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
//   constexpr T fn(StructType const& s) noexcept {
//       CRUCIBLE_PRE(invariant_holds(s));
//       // ... body trusts the invariant via [[assume]] ...
//   }
//
// The macro can also be used as a `contract_assert`-style mid-body
// invariant check:
//
//   void f() noexcept {
//       CRUCIBLE_PRE(global_invariant());
//       ...
//   }
//
// ───────────────────────────────────────────────────────────────────
// COST MODEL
// ───────────────────────────────────────────────────────────────────
//
//   Build mode    | Compile-time check | Runtime check | Optimizer hint
//   --------------|--------------------|----------------|----------------
//   NDEBUG (rel)  | YES (consteval)    | NO             | YES ([[assume]])
//   !NDEBUG (dbg) | YES (consteval)    | YES (abort)    | YES ([[assume]])
//
// The consteval branch is `if consteval { ... }` which is a compile-time
// discriminator; at runtime the entire branch is dead code, eliminated
// before codegen.  So NDEBUG production binaries pay zero bytes for the
// consteval enforcement.
//
// ───────────────────────────────────────────────────────────────────
// COMPARISON WITH P2900 `pre()` CLAUSES
// ───────────────────────────────────────────────────────────────────
//
//                          | P2900 pre()    | CRUCIBLE_PRE
//   -----------------------|----------------|-------------
//   Runtime enforce        | yes            | yes (debug only)
//   Consteval enforce      | partial / no   | yes (always)
//   Zero cost in NDEBUG    | yes            | yes
//   Optimizer hint         | yes            | yes
//   Works on struct ref    | NO             | yes
//   Works on struct value  | NO             | yes
//   Documents the contract | yes            | yes (in body, near use)
//   Compatible with [[gnu::pure]] | yes     | yes
//
// The pre()/post() clauses still belong on PUBLIC API entry points
// where runtime enforcement under semantic=enforce is the load-bearing
// guarantee.  CRUCIBLE_PRE is the right tool for:
//   * any function that needs its contract to fire at consteval
//   * any function whose pre clause reads through a struct parameter
//   * any function called from a static_assert in a neg-compile fixture
//
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE AXIOMS
// ───────────────────────────────────────────────────────────────────
// InitSafe, TypeSafe — predicate is a bare C++ expression; same idioms
//   as a bare `if (!cond)`.
// NullSafe — `[[assume(cond)]]` propagates the non-null property to the
//   optimizer; downstream loads on the param can be hoisted/simplified.
// MemSafe — no allocation, no destructor, no exception.
// BorrowSafe / ThreadSafe — no shared state.
// LeakSafe — no resource handle.
// DetSafe — predicate is evaluated at most once; result is observable.

#pragma once

#include <crucible/Platform.h>   // NDEBUG plumbing

#include <cstdlib>               // std::abort

// ─── CRUCIBLE_PRE ──────────────────────────────────────────────────
//
// Form: `CRUCIBLE_PRE(expression)` — evaluates `expression` as bool;
// if false, fails the call.  Always emits `[[assume(expression)]]`
// after the check so the optimizer propagates the invariant downstream.

#ifdef NDEBUG

  // Production / release: consteval check only, plus [[assume]] hint.
  // At runtime, the `if consteval` block is statically false; the
  // entire body is eliminated by codegen.  Zero bytes, zero cycles.
  #define CRUCIBLE_PRE(cond)                                            \
      do {                                                              \
          if consteval {                                                \
              if (!(cond)) [[unlikely]] { __builtin_trap(); }           \
          }                                                             \
          [[assume(cond)]];                                             \
      } while (0)

#else

  // Debug / test: consteval check + runtime check + [[assume]] hint.
  // The `if consteval` discriminator splits the trap path: at consteval
  // time, `__builtin_trap()` is a non-constexpr call → "non-constant
  // condition" diagnostic.  At runtime, std::abort() fires SIGABRT.
  #define CRUCIBLE_PRE(cond)                                            \
      do {                                                              \
          if (!(cond)) [[unlikely]] {                                   \
              if consteval { __builtin_trap(); }                        \
              else         { std::abort(); }                            \
          }                                                             \
          [[assume(cond)]];                                             \
      } while (0)

#endif
