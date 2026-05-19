#pragma once

// ── crucible::fixy — Profile.h — strict / sketch mode selector ─────
//
// Scaffold per misc/16_05_2026_fixy.md §82, §132, §347-369.
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
// ── Integration status (fixy-A4-001 sweep, 2026-05-18) ────────────
//
// This header ships the PROFILE TOGGLE INFRASTRUCTURE AND its wrapper
// integration is now live.  `fixy/Fn.h` consumes `IsAcceptedActive`
// at the `mint_fn` requires-clause, and the class-body 5-tier
// `static_assert` chain short-circuits tier 3 (AllDimsEngaged) and
// tier 5 (NotInTheoryCorpus) under `!fixy_is_strict`.  Tiers 1
// (Type validity), 2 (grant well-formedness), and 4 (unique
// engagement / §6.8 collision rules) stay strict in BOTH modes —
// sketch mode relaxes the engagement axis + theory-corpus checks
// only, never the collision-correctness floor.
//
// Post-fixy-H-05, `IsAccepted` is the wrapper-discipline gate
// (auto-injects the Type-axis marker); the legacy `IsAcceptedFn`
// alias has been removed.  Sentinel TU `test/test_fixy_umbrella_reach`
// witnesses both the Profile.h reach via `<crucible/Fixy.h>` and the
// toggle-bound `IsAcceptedActive` routing through `mint_fn`.
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
// ── IsAcceptedSketch — the Type-axis-only concept ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Sketch-mode bindings route through this gate instead of `IsAccepted`
// so a partially-specified Grants pack does not stop compilation.  The
// binding STILL goes through the substrate's `safety::fn::Fn<...>`
// class-body static_assert chain when `fixy::fn` instantiates
// `resolved_fn_t`: sketch mode permissivity applies ONLY to the
// engagement axis + theory-corpus checks, never to the §6.8 collision
// rules NOR to Type-axis structural validity.
//
// fixy-A4-031 — Type-axis structural validity preserved in BOTH modes.
// The prior shape `concept IsAcceptedSketch = true;` was always-true:
// `IsAcceptedSketch<void>` succeeded at the `mint_fn` requires-clause
// even though the substrate's tier-1 static_assert would reject
// `Fn<void, ...>` downstream with a far less actionable diagnostic
// (consteval-bypass on foldable bodies, or a deep nested error inside
// `safety::fn::Fn`).  The fix delegates Type-axis structural validity
// to the canonical `detail::accept::type_is_accepted_payload<Type>()`
// predicate (Reject.h §fixy-H-10) — the same gate that `IsAccepted`
// uses for the Type axis.  Result: void / cv-qualified / reference /
// array / bare-function-type payloads reject at the `mint_fn` call
// site under both STRICT and SKETCH, with an `IsAcceptedSketch`-named
// constraint-failure diagnostic instead of a substrate-internal one.
//
// Grant-axis permissivity is the entire point of sketch mode — the
// `Grants...` pack is NOT inspected by this concept (no engagement
// check, no theory-corpus check).  The tier-2 `AllGrantsWellFormed`
// static_assert in `Fn`'s class body remains strict in both modes per
// Profile.h §"Integration status" — malformed grants reject even
// under SKETCH.
//
// Production rule of thumb: if a TU compiles green under SKETCH but
// red under STRICT, the binding has unengaged dims or matches a
// §30.14 pattern — the work-in-progress is incomplete, not a bug.
// If a TU rejects under SKETCH, the issue is either (a) malformed
// grant (tier-2) or (b) malformed Type payload (this concept) —
// neither is a partial-specification problem and both must be fixed
// before strict-mode CI passes.

template <typename Type, typename... Grants>
concept IsAcceptedSketch =
    detail::accept::type_is_accepted_payload<Type>();

// ═════════════════════════════════════════════════════════════════════
// ── IsAcceptedActive — the toggle-bound active gate ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Aliases to `IsAccepted` under STRICT, `IsAcceptedSketch` under
// SKETCH.  Post-fixy-H-05, `IsAccepted` is the wrapper-discipline
// gate that auto-injects the Type-axis marker, so `IsAcceptedActive`
// inherits the same shape.  `fixy/Fn.h::mint_fn`'s requires-clause
// consumes this alias; the class-body tier 3 + tier 5 asserts
// short-circuit under `!fixy_is_strict` (see Fn.h for the exact
// fixy_h02_tier{3,5}_* preconditions).
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

// 2. IsAcceptedSketch is permissive on the Grants axis but strict on
//    the Type axis (fixy-A4-031).  Grant packs of any shape accept;
//    void / cv-qualified / reference / array / bare-function-type
//    payloads reject.

// ── Type-axis ACCEPT witnesses ────────────────────────────────────
// Scalars, object pointers, and function pointers are accepted
// payloads regardless of Grants pack content.
static_assert(IsAcceptedSketch<int>,
    "IsAcceptedSketch<int> must accept the empty Grants pack — sketch "
    "mode is permissive on the Grants axis.");
static_assert(IsAcceptedSketch<int*>,
    "IsAcceptedSketch<int*> must accept — object pointers are accepted "
    "payloads.");
static_assert(IsAcceptedSketch<int(*)(int)>,
    "IsAcceptedSketch<int(*)(int)> must accept — function POINTERS are "
    "object types, hence accepted payloads.");

// ── Type-axis REJECT witnesses (fixy-A4-031) ──────────────────────
// Sketch mode does NOT relax Type-axis structural validity.  Each
// of these would otherwise produce a far less actionable diagnostic
// deep inside the substrate's `Fn<Type, ...>` class body.
static_assert(!IsAcceptedSketch<void>,
    "fixy-A4-031: IsAcceptedSketch<void> must reject — Fn<void, ...> "
    "has no value-category semantics; sketch mode does not bypass the "
    "Type-axis floor.");
static_assert(!IsAcceptedSketch<const int>,
    "fixy-A4-031: top-level const-qualified Type must reject — silently "
    "deletes Fn's defaulted assignment ops.");
static_assert(!IsAcceptedSketch<volatile int>,
    "fixy-A4-031: top-level volatile-qualified Type must reject for "
    "the same reason as const.");
static_assert(!IsAcceptedSketch<int&>,
    "fixy-A4-031: lvalue-reference Type must reject — Fn<int&, ...> "
    "has no clear copy/move semantics.");
static_assert(!IsAcceptedSketch<int&&>,
    "fixy-A4-031: rvalue-reference Type must reject for the same "
    "reason as lvalue-reference.");
static_assert(!IsAcceptedSketch<int[5]>,
    "fixy-A4-031: array Type must reject — Fn(Type) would silently "
    "decay to pointer instead of copy by value.");
static_assert(!IsAcceptedSketch<int(int)>,
    "fixy-A4-031: bare function-type Type must reject — wrap as "
    "function pointer or callable before instantiating fixy::fn.");

// ── Grant-axis permissivity preserved ─────────────────────────────
// A partially-specified or even empty Grants pack still accepts so
// long as the Type axis is structurally valid.  Sketch mode's whole
// purpose is to let migrating TUs compile while their Grants pack is
// still being filled in.
namespace not_a_grant_tag { struct Tag {}; }
static_assert(IsAcceptedSketch<int, not_a_grant_tag::Tag>,
    "IsAcceptedSketch ignores Grants-pack shape — even a non-grant "
    "type in the pack accepts as long as Type is structurally valid. "
    "(Tier-2 AllGrantsWellFormed in Fn's class body still rejects the "
    "binding downstream, but THIS concept does not.)");

}  // namespace detail::profile_self_test

}  // namespace crucible::fixy
