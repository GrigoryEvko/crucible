// SPDX-License-Identifier: Apache-2.0
// crucible — safety/Pre.h
//
// CRUCIBLE_PRE — precondition macro that fires at consteval AND runtime,
// regardless of parameter shape, with rich diagnostics on violation.
//
// ───────────────────────────────────────────────────────────────────
// WHY THIS HEADER EXISTS (per misc/08_05_2026_harness.md §3.1, §4)
// ───────────────────────────────────────────────────────────────────
// On the un-patched distro GCC 16.1.1 (May-1 build), P2900 pre()/post()
// clauses silently bypass at consteval for 6 of 7 parameter shapes;
// only `pre (p != nullptr)` on a const-pointer fires. The patched
// build (PR c++/124241 cherry-picked, May-4 cache fix; see misc/
// 08_05_2026_harness.md §0) closes the bypass for all 7 shapes —
// but introduces its own regression on always-true post-clauses on
// foldable-body constexpr functions (§13.6). CRUCIBLE_PRE is shipped
// as DEFENSE-IN-DEPTH: identical behavior across both builds, zero
// NDEBUG cost, richer diagnostics than native pre()/post() emit.
//
// Mechanism: macro lives in the function BODY, not the parser-special
// pre-clause position, so it bypasses the constexpr-cache machinery
// entirely. A `__builtin_trap()` planted inside `if consteval` is
// non-constexpr, poisoning the surrounding consteval call into
// "non-constant condition" — which is what static_assert reports.
// At runtime, `crucible::detail::contract_failed()` prints the
// failed expression + source location and aborts via the same
// debugger-aware path as `handle_contract_violation`.
//
// ───────────────────────────────────────────────────────────────────
// USAGE
// ───────────────────────────────────────────────────────────────────
//   constexpr T fn(StructType const& s) noexcept {
//       CRUCIBLE_PRE(invariant_holds(s));
//       // body trusts the invariant via [[assume]]
//   }
//
// On runtime violation under !NDEBUG, debug build emits:
//   crucible: contract violation: invariant_holds(s)
//     at /path/to/file.cpp:42 in T fn(StructType const&)
// then aborts via std::abort() (after debugger-aware breakpoint).
//
// On consteval violation (e.g. inside static_assert), GCC emits:
//   error: non-constant condition for static assertion
//   note: ... __builtin_trap() called ...
//
// The macro can also be used as a `contract_assert`-style mid-body
// invariant check; the spelling is identical because the semantic is
// identical (predicate must hold at this program point).
//
// ───────────────────────────────────────────────────────────────────
// COST MODEL
// ───────────────────────────────────────────────────────────────────
//
//   Build mode    | Compile-time check | Runtime check | Optimizer hint
//   --------------|--------------------|----------------|----------------
//   NDEBUG (rel)  | YES (consteval)    | NO             | YES ([[assume]])
//   !NDEBUG (dbg) | YES (consteval)    | YES (routed)   | YES ([[assume]])
//
// The consteval branch is `if consteval { ... }`, a compile-time
// discriminator; at runtime the entire branch is dead code,
// eliminated before codegen. NDEBUG production binaries pay zero
// bytes for the consteval enforcement.
//
// Locked by test/test_pre_post.cpp + test/test_pre_post_cost.cpp:
//   * positive constexpr witnesses for 9 parameter shapes
//   * NDEBUG vs !NDEBUG codegen-size assertion (zero-cost claim)
//
// ───────────────────────────────────────────────────────────────────
// COMPARISON WITH P2900 pre()/post() CLAUSES (post-patch)
// ───────────────────────────────────────────────────────────────────
//
//                          | P2900 pre() (patched) | CRUCIBLE_PRE
//   -----------------------|------------------------|-------------
//   Runtime enforce        | yes (semantic-driven)  | yes (debug only)
//   Consteval enforce      | yes (all 7 shapes)     | yes (all shapes)
//   Zero cost in NDEBUG    | yes                    | yes
//   Optimizer hint         | yes                    | yes ([[assume]])
//   Predicate text in diag | comment() (parser)     | #cond stringify
//   Source location in diag| std::source_location   | __builtin_FILE/LINE
//   Handler routing        | handle_contract_violation | crucible::detail::contract_failed
//   Foldable-body post     | broken (§13.6)         | works
//   Cross-build portable   | no (un-patched bypass) | yes
//   Compatible w/ [[gnu::const]] / [[gnu::pure]] | yes (NDEBUG-side compatible*) | yes (NDEBUG-side; side effects in debug)
//
//   * NDEBUG branch in CRUCIBLE_PRE is pure ([[assume]] only); debug
//     branch calls contract_failed which has side effects. Functions
//     marked [[gnu::const]] or [[gnu::pure]] should use this macro
//     only when NDEBUG-only enforcement is acceptable, OR when the
//     debug-side abort path is acceptable for production debug runs.
//
// On the patched build, native pre() and CRUCIBLE_PRE are nearly
// equivalent. CRUCIBLE_PRE wins on:
//   * cross-build portability (un-patched distro builds)
//   * §13.6 foldable-body regression immunity
//   * #cond stringification (P2900 reports the parsed comment, which
//     can be scoped-name-decayed by the parser)
// P2900 wins on:
//   * declaration discoverability (clauses appear in signature)
//   * `quick_enforce` semantic for handler-skipping fast abort
//
// Use both: P2900 on public API entry points where the signature is
// the contract; CRUCIBLE_PRE for defense-in-depth, mid-body
// invariants, and cross-build portability.
//
// ───────────────────────────────────────────────────────────────────
// VC DISCHARGE FRAMING
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE_PRE / CRUCIBLE_POST are the production-level discharge
// mechanism for verification conditions (VCs) that the type system
// cannot statically prove.  Every callsite of a Refined / Tagged /
// Linear / Monotonic / Bits / Permission wrapper has a mathematical
// pre/postcondition; CRUCIBLE_PRE is the syntactic surface where
// that obligation is discharged at the boundary.  Three layers
// stack:
//
//   1. Type-level proof (always-discharge):
//      `Refined<bounded_above<8>, uint8_t>` proves at construction
//      that the wrapped value is in [0, 8].  Downstream functions
//      that take `Refined<...>` need NO pre clause — the type IS
//      the proof.  This is the cheapest, most preferred form.
//
//   2. Named predicate cite (catalog discharge):
//      `CRUCIBLE_PRE(decide::in_range<uint8_t>(idx, 0, 7))` —
//      one of 14 named predicates in safety/Decide.h (CONTRACT-020
//      catalog).  Names are grep-discoverable; future hardening
//      (lifting `idx` to `Refined`) propagates through the predicate
//      name once.  Cite-discipline migrations follow CONTRACT-100..
//      127 commit-message tags.
//
//   3. Anonymous predicate (one-off discharge):
//      `CRUCIBLE_PRE(p != nullptr && p->ready)` — direct expression,
//      no catalog cite.  Acceptable when the predicate is genuinely
//      bespoke (mid-body invariant on transient state) but the
//      CONTRACT-* migration sweep prefers (2) so audits can count
//      "operations guarded against integer overflow" via
//      `grep decide::no_overflow_sum`.
//
// Postconditions follow the same layering: every load-bearing
// state-mutation gets a `CRUCIBLE_POST` cite, and the dual-side
// discipline (CONTRACT-100..108-POST + 116..127-POST + Tx + IterDet
// + CKernel sweeps) requires that EVERY new CONTRACT-* migration
// audit BOTH pre and post, skipping post only with documented
// rationale (tautological / racy / structurally-not-guaranteed).
//
// The dual-side discipline catches a class of bug that pre-only
// migrations miss: state-mutating operations whose post-state
// invariant has no pre-state witness.  E.g., commit() takes a tx
// pointer (pre-validated non-null) but produces tx->status ==
// COMMITTED — a post that catches a future refactor dropping the
// status assignment.  See feedback_pre_post_dual_discipline.md
// for the full pattern.
//
// Two known traps codified by the discipline:
//   * Disjunction-vs-implies for null-guarded post (deref-safe
//     post pattern): `decide::implies(p != nullptr, p->status ==
//     X)` evaluates BOTH args eagerly under C++ function-call
//     semantics — `p->status` derefs null when p is null.  Use
//     short-circuit `||` form (`p == nullptr || p->status == X`)
//     when the consequent dereferences a witnessed non-null
//     pointer.  See feedback_decide_implies_eager_eval.md.
//   * Consteval-bypass on `this->` member predicates (GCC 16.1.1):
//     P2900 `pre()` / `post (r:...)` referencing class members
//     through `this->` silently bypasses at consteval for foldable
//     bodies.  Migrate to in-body CRUCIBLE_PRE / CRUCIBLE_POST.
//
// ───────────────────────────────────────────────────────────────────
// CRUCIBLE AXIOMS
// ───────────────────────────────────────────────────────────────────
// InitSafe, TypeSafe — predicate is a bare C++ expression; same
//   idioms as a plain `if (!cond)`.
// NullSafe — `[[assume(cond)]]` propagates the non-null (or any
//   refinement) property to the optimizer; downstream loads on the
//   refined value can be hoisted/simplified.
// MemSafe — no allocation, no destructor, no exception.
// BorrowSafe / ThreadSafe — no shared state.
// LeakSafe — no resource handle.
// DetSafe — predicate is evaluated at most once; result is observable
//   only via the structured branch (trap / contract_failed / assume).

#pragma once

#include <crucible/Platform.h>   // NDEBUG plumbing

namespace crucible::detail {

// Routed runtime-violation entry points. Defined in src/ContractHandler.cpp
// alongside the weak handle_contract_violation. Both mirror that handler's
// diagnostic + debugger-aware abort discipline so debug output is
// uniform across CRUCIBLE_PRE/POST/PRE_FAST/POST_FAST and native P2900
// pre()/post()/contract_assert paths.
//
// Parameters (cheap form):
//   expr — predicate text (from `#cond` stringification)
//   file — source file at the call site (from __builtin_FILE())
//   line — source line at the call site (from __builtin_LINE())
//   fn   — enclosing function signature (from __PRETTY_FUNCTION__)
//
// Behavior: prints "crucible: contract violation: <expr>\n  at <file>
// :<line> in <fn>\n" to stderr, emits a C++23 stack trace (when
// libstdc++ ships <stacktrace>), calls breakpoint_if_debugging() (no-op
// when no debugger attached), then std::abort(). Marked [[gnu::cold]]
// so the call site doesn't pollute hot icache; [[noreturn]] lets the
// optimizer treat the post-call code as unreachable.
[[noreturn, gnu::cold]]
void contract_failed(char const* expr,
                     char const* file,
                     int line,
                     char const* fn) noexcept;

// Annotated form — adds a "note: <msg>" line between the source
// location and the stack trace.  Routed by CRUCIBLE_PRE_MSG and
// CRUCIBLE_POST_MSG when the call site supplies a message string.
[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr,
                         char const* file,
                         int line,
                         char const* fn,
                         char const* msg) noexcept;

}  // namespace crucible::detail

// ─── CRUCIBLE_PRE ──────────────────────────────────────────────────
//
// Form: `CRUCIBLE_PRE(expression)` — evaluates `expression` as bool;
// if false, fails the call (consteval poison or runtime abort).
// Always emits `[[assume(expression)]]` after the check so the
// optimizer propagates the invariant downstream.
//
// The consteval branch is `if consteval { ... }`, a C++23 compile-time
// discriminator. At consteval evaluation, the branch executes and
// __builtin_trap() (a non-constexpr call) poisons the surrounding
// expression into "non-constant condition" — which is exactly what
// static_assert reports as a hard error. At runtime, the branch is
// dead code, eliminated before codegen.

#ifdef NDEBUG

  // Production / release: consteval check only, plus [[assume]] hint.
  // At runtime, the entire `if consteval` block is statically false;
  // the body is eliminated by codegen. Zero bytes, zero cycles.
  //
  // Note: the predicate is still evaluated at consteval, so a
  // failing static_assert that uses CRUCIBLE_PRE still fires under
  // NDEBUG. This is the load-bearing property — neg-compile fixtures
  // work the same in release and debug builds.
  #define CRUCIBLE_PRE(cond)                                            \
      do {                                                              \
          if consteval {                                                \
              if (!(cond)) [[unlikely]] { __builtin_trap(); }           \
          }                                                             \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#else

  // Debug / test: consteval check + runtime check + [[assume]] hint.
  // The `if consteval` discriminator splits the trap path: at consteval
  // time, `__builtin_trap()` is a non-constexpr call → "non-constant
  // condition" diagnostic. At runtime, contract_failed() prints the
  // diagnostic and aborts.
  //
  // The runtime path uses __builtin_FILE() / __builtin_LINE() rather
  // than __FILE__ / __LINE__ macros because builtins resolve at the
  // call site naturally and compose with future defaulted-argument
  // refactors. __PRETTY_FUNCTION__ is the GCC extension for the
  // enclosing function's full signature (matches std::source_location::
  // function_name() output).
  #define CRUCIBLE_PRE(cond)                                            \
      do {                                                              \
          if (!(cond)) [[unlikely]] {                                   \
              if consteval { __builtin_trap(); }                        \
              else {                                                    \
                  ::crucible::detail::contract_failed(                  \
                      #cond, __builtin_FILE(),                          \
                      __builtin_LINE(), __PRETTY_FUNCTION__);           \
              }                                                         \
          }                                                             \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#endif

// ─── CRUCIBLE_PRE_FAST ─────────────────────────────────────────────
//
// Hot-path variant of CRUCIBLE_PRE.  Mirrors P2900's `quick_enforce`
// semantic — skip the handler, trap directly.  Use when:
//
//   * The function is on a measured hot path where the ~µs of
//     stderr flush + breakpoint_if_debugging() overhead in
//     `crucible::detail::contract_failed` is unacceptable.
//   * Diagnostic loss is acceptable: violation produces SIGABRT
//     (or core dump) without a printed message.  Production
//     debugging falls back to the core dump's stack trace.
//   * Cross-build portability still required (the macro form
//     dodges the un-patched-distro bypass for the same reasons
//     CRUCIBLE_PRE does).
//
// Cost model identical to CRUCIBLE_PRE in NDEBUG (zero); in !NDEBUG
// the runtime branch is a single `__builtin_trap()` call instead
// of the contract_failed call.  No format-string parsing, no I/O,
// no debugger hook — minimum overhead violation path.
//
// Use sparingly.  Default to CRUCIBLE_PRE; reach for FAST only
// when you've measured the diagnostic cost matters.
#ifdef NDEBUG

  #define CRUCIBLE_PRE_FAST(cond)                                       \
      do {                                                              \
          if consteval {                                                \
              if (!(cond)) [[unlikely]] { __builtin_trap(); }           \
          }                                                             \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#else

  #define CRUCIBLE_PRE_FAST(cond)                                       \
      do {                                                              \
          if (!(cond)) [[unlikely]] { __builtin_trap(); }               \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#endif

// ─── CRUCIBLE_PRE_MSG ──────────────────────────────────────────────
//
// Annotated variant.  Same semantics as CRUCIBLE_PRE; the runtime
// path additionally emits a "note: <msg>" line between the source
// location and the stack trace.  Use when the predicate text alone
// is cryptic and would benefit from a one-line explanation:
//
//   CRUCIBLE_PRE_MSG(buf.size() % recipe.granularity == 0,
//                    "buffer length must be a multiple of recipe granularity");
//
// On violation, runtime emits:
//   crucible: contract violation: buf.size() % recipe.granularity == 0
//     at <file>:<line> in <fn>
//     note: buffer length must be a multiple of recipe granularity
//     stack trace: ...
//
// At consteval the message is unused (the trap diagnostic carries
// no extra text either way); the macro behaves identically to
// CRUCIBLE_PRE for static_assert-fired neg-compile fixtures.
#ifdef NDEBUG

  #define CRUCIBLE_PRE_MSG(cond, msg)                                   \
      do {                                                              \
          if consteval {                                                \
              if (!(cond)) [[unlikely]] { __builtin_trap(); }           \
          }                                                             \
          (void)(msg);                                                  \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#else

  #define CRUCIBLE_PRE_MSG(cond, msg)                                   \
      do {                                                              \
          if (!(cond)) [[unlikely]] {                                   \
              if consteval { __builtin_trap(); }                        \
              else {                                                    \
                  ::crucible::detail::contract_failed_msg(              \
                      #cond, __builtin_FILE(),                          \
                      __builtin_LINE(), __PRETTY_FUNCTION__, (msg));    \
              }                                                         \
          }                                                             \
          CRUCIBLE_CONTRACT_FENCE_();                                   \
          [[assume(cond)]];                                             \
      } while (0)

#endif

// ─── Observable-checkpoint hardening (opt-in) ──────────────────────
//
// Per misc/08_05_2026_harness.md §4.7, P1494R5 + P3641R0 ship
// `__builtin_observable_checkpoint()` (compiler intrinsic, zero
// machine cost — pure optimizer fence) and `std::observable_checkpoint()`
// (C++26 wrapper).  They mark a sequence point that the optimizer
// is forbidden to backward-reorder UB-reasoning across.
//
// Why this matters for [[assume]]: aggressive UB-based optimization
// is allowed to delete code BEFORE an `[[assume(cond)]]` under "if
// cond is false here, the prior code couldn't have happened"
// reasoning.  Technically sound under the abstract machine but
// produces spooky-action-at-a-distance bugs.  An observable checkpoint
// inserted between the runtime check and the `[[assume]]` blocks
// that backward reasoning at the checkpoint boundary.
//
// In practice, GCC 16's optimizer is non-aggressive about backward-
// UB reasoning past `[[assume]]`, so this hardening is unobserved-
// to-matter today.  Opt-in via `-DCRUCIBLE_CONTRACT_OBSERVABLE` for
// security-sensitive translation units (Cipher key derivation,
// crypto paths, secret-handling) where the theoretical hole has
// real cost.
//
// Zero machine cost; pure compile-time barrier.  The runtime check
// path is unchanged; only the `[[assume]]`-side guard tightens.
#if defined(CRUCIBLE_CONTRACT_OBSERVABLE)
  #define CRUCIBLE_CONTRACT_FENCE_() __builtin_observable_checkpoint()
#else
  #define CRUCIBLE_CONTRACT_FENCE_() ((void)0)
#endif
