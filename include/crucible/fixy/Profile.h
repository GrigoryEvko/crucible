#pragma once

// ── crucible::fixy — Profile.h — strict / sketch mode selector ─────
//
// Phase D scaffold per misc/16_05_2026_fixy.md §82, §132, §347-369.
//
// fixy ships in TWO acceptance modes, selected at CMake configure time
// via the cache var `CRUCIBLE_FIXY_STRICT` (default ON):
//
//   ── STRICT (default; `CRUCIBLE_FIXY_STRICT=ON`) ─────────────────
//   The production discipline.  Every binding routes through
//   `IsAccepted<Type, Grants...>` (Reject.h) — every dim must be
//   engaged via an explicit relaxation OR an
//   `accept_default_strict_for<Dim>` marker, every Grant must be
//   `IsGrantTag`, every axis must be engaged at most once, and no
//   §30.14 corpus pattern may match.  Mismatches are HARD COMPILE
//   ERRORS via `static_assert` / `requires`-clause failure.
//
//   ── SKETCH (`CRUCIBLE_FIXY_STRICT=OFF`) ──────────────────────────
//   The hot-loop migration discipline.  IsAccepted's gate is replaced
//   by `IsAcceptedSketch` — a concept that ALWAYS succeeds — so a
//   greenfield TU dropping `fixy::fn<T, partially-specified-grants...>`
//   compiles even when the binding would fail strict.  The relaxation
//   surfaces as a `[[deprecated]]`-style warning at the call site
//   pointing the migrating engineer at the offending axis(es); CI on
//   the same TU under STRICT mode still rejects.
//
//   Use case: hot-loop refactor where every binding will EVENTUALLY
//   carry a full 20-axis Grants pack, but the migration is itself a
//   multi-commit sweep.  Sketch mode lets the work-in-progress TUs
//   build green under `default` preset while the discipline harness
//   (CI matrix) verifies the strict variant on the same source.
//
// ── Scope note (B2 ship) ───────────────────────────────────────────
//
// This header ships the PROFILE TOGGLE INFRASTRUCTURE.  It does NOT
// rewire `fixy::fn`'s class-body `static_assert` to use the toggle —
// that is the follow-up commit's job, where the production wrappers
// route through `IsAcceptedActive<Type, Grants...>` (the alias defined
// below) instead of `IsAcceptedFn` directly.  The split keeps the two
// changes auditable: B2 introduces the mechanism + CMake glue + the
// neg-compile sentinel; the integration commit threads the mechanism
// through the wrapper.
//
// ── CMake wiring ───────────────────────────────────────────────────
//
// In the top-level CMakeLists.txt:
//
//   option(CRUCIBLE_FIXY_STRICT
//     "Build fixy:: in STRICT mode (default).  Set OFF to enable "
//     "SKETCH mode for hot-loop migration." ON)
//   if(CRUCIBLE_FIXY_STRICT)
//     target_compile_definitions(crucible INTERFACE
//       CRUCIBLE_FIXY_STRICT=1)
//   else()
//     target_compile_definitions(crucible INTERFACE
//       CRUCIBLE_FIXY_STRICT=0)
//   endif()
//
// The INTERFACE scope ensures every consumer of `crucible` (test +
// library + downstream) compiles against the same toggle.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   fixy/Reject.h::IsAccepted             — strict gate
//
// ── Substrate added by this header ─────────────────────────────────
//
// One concept (`IsAcceptedSketch`), one alias (`IsAcceptedActive`),
// and one constexpr bool sentinel (`fixy_is_strict`).  Zero runtime
// cost; every concept resolves at template instantiation.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — IsAcceptedActive is a structural concept alias, no
//              implicit conversion path.
//   InitSafe — pure compile-time computation.
//   DetSafe  — same toggle setting → same active concept → same
//              federation cache key (per GAPS-028 row_hash discipline).
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Two compile-time witnesses ride this header:
//   1. `fixy_is_strict` reflects the CMake toggle's current value.
//   2. `IsAcceptedSketch<T, Grants...>` accepts ANY arguments (the
//      permissive shape of the sketch concept).

#include <crucible/fixy/Reject.h>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_FIXY_STRICT — defaulted to ON by the CMake glue ───────
// ═════════════════════════════════════════════════════════════════════
//
// The CMake target propagates `CRUCIBLE_FIXY_STRICT=1` or `=0` via
// `target_compile_definitions(crucible INTERFACE ...)`.  If the macro
// is somehow undefined at the TU (third-party consumer not wired to
// the CMake target), default to STRICT — the safe choice.

#ifndef CRUCIBLE_FIXY_STRICT
#define CRUCIBLE_FIXY_STRICT 1
#endif

// ─── fixy_is_strict — compile-time witness for the toggle ──────────

inline constexpr bool fixy_is_strict = (CRUCIBLE_FIXY_STRICT != 0);

// ═════════════════════════════════════════════════════════════════════
// ── IsAcceptedSketch — the permissive concept ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Always-true concept.  Sketch-mode bindings route through this gate
// instead of `IsAccepted` so a partially-specified Grants pack does
// not stop compilation.  The binding STILL goes through the substrate's
// `safety::fn::ValidComposition` once Resolve.h projects to Fn<...>
// — sketch mode permissivity applies only to the engagement axis +
// theory-corpus checks, never to the §6.8 collision rules.
//
// Production rule of thumb: if a TU compiles green under SKETCH but
// red under STRICT, the binding has unengaged dims or matches a
// §30.14 pattern — the work-in-progress is incomplete, not a bug.

template <typename Type, typename... Grants>
concept IsAcceptedSketch = true;

// ═════════════════════════════════════════════════════════════════════
// ── IsAcceptedActive — the toggle-bound active gate ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Aliases to `IsAccepted` under STRICT, `IsAcceptedSketch` under
// SKETCH.  The next-commit follow-up rewires `fixy::fn`'s class-body
// `static_assert` from `IsAcceptedFn` to a Profile-aware variant
// `IsAcceptedActiveFn` that injects the ImplicitTypeMarker and routes
// through `IsAcceptedActive`.  Until that integration ships, this
// alias is the infrastructure-only surface.
//
// Cost-of-violation: a binding accepted under SKETCH still passes
// every other static_assert in `safety::fn::Fn<...>`'s class body
// (the §6.8 collision rules); sketch mode does NOT bypass collision
// correctness.  Only the engagement + theory-corpus check is relaxed.

template <typename Type, typename... Grants>
concept IsAcceptedActive =
#if CRUCIBLE_FIXY_STRICT
    IsAccepted<Type, Grants...>;
#else
    IsAcceptedSketch<Type, Grants...>;
#endif

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — compile-time witnesses ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::profile_self_test {

// 1. fixy_is_strict reflects the CMake toggle.
//    Under default preset, CRUCIBLE_FIXY_STRICT=1 → fixy_is_strict==true.
#if CRUCIBLE_FIXY_STRICT
static_assert(fixy_is_strict,
    "Profile.h: fixy_is_strict must be true when "
    "CRUCIBLE_FIXY_STRICT=1.");
#else
static_assert(!fixy_is_strict,
    "Profile.h: fixy_is_strict must be false when "
    "CRUCIBLE_FIXY_STRICT=0.");
#endif

// 2. IsAcceptedSketch is the always-true concept.
static_assert(IsAcceptedSketch<int>,
    "IsAcceptedSketch<int> must accept the empty Grants pack — sketch "
    "mode is permissive by design.");

static_assert(IsAcceptedSketch<void>,
    "IsAcceptedSketch<void> must accept — sketch mode does not check "
    "Type-axis well-formedness.");

}  // namespace detail::profile_self_test

}  // namespace crucible::fixy
