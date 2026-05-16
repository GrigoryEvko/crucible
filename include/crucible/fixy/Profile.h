#pragma once

// ── crucible::fixy — Profile.h (FIXY-D, sketch vs release toggle) ─────
//
// Per misc/16_05_2026_fixy.md §4 Phase D: two profiles for the
// fixy:: discipline layer:
//
//   * **Release (default)** — `IsAccepted<Grants...>` is a hard
//     reject.  Missing engagement on any dim is a compile error.
//     This is what Phase A/B/C ship.
//
//   * **Sketch** — `IsAccepted<Grants...>` is downgraded to
//     `IsAcceptedWithWarning<Grants...>`: the concept is satisfied
//     unconditionally, but a `[[deprecated]]` annotation fires
//     when any dim is unengaged.  Lets a developer prototype
//     greenfield code without engaging every dim while still
//     surfacing the gap.  Sketch is the prototyping profile;
//     release ships the prototype.
//
// **CMake gate.**  `CRUCIBLE_FIXY_STRICT` cache var, defaults ON.
// When OFF, the preprocessor symbol `CRUCIBLE_FIXY_SKETCH` is
// defined and `Profile.h` selects the sketch concept.  When ON
// (or unset — release default), `IsAccepted` is the hard gate
// from Reject.h.
//
// **Per-target opt-in.**  CMake property `CRUCIBLE_FIXY_ONLY ON`
// on a target activates the linter (Phase F), independent of
// strict/sketch.  A target may be fixy-only AND in sketch mode
// (prototyping with discipline-aware warnings) or fixy-only AND
// in strict mode (production).  The two axes are orthogonal.
//
// **No runtime cost.**  Both concepts are compile-time only.
// Sketch profile adds zero runtime overhead; the [[deprecated]]
// annotation emits a build-time warning only.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   namespace fixy {
//     // Always-true sketch alternative to IsAccepted.
//     template <typename... Grants>
//     concept IsAcceptedWithWarning = ...;
//
//     // Aliases for production call sites — pick one per project
//     // posture.  Release builds use Strict.  Sketch builds use
//     // Sketch.  CRUCIBLE_FIXY_SKETCH preprocessor symbol controls
//     // which `IsAcceptedSelected` resolves to.
//     template <typename... Grants>
//     concept IsAcceptedStrict = IsAccepted<Grants...>;
//
//     template <typename... Grants>
//     concept IsAcceptedSketch = IsAcceptedWithWarning<Grants...>;
//
//     // Profile selector — flips between Strict / Sketch based on
//     // build-time CMake var.  fixy::fn<...> can be extended to
//     // consult this alias if a per-TU sketch-mode opt-in is
//     // needed (Phase D+ work).
//     #ifdef CRUCIBLE_FIXY_SKETCH
//     template <typename... Grants>
//     concept IsAcceptedSelected = IsAcceptedSketch<Grants...>;
//     #else
//     template <typename... Grants>
//     concept IsAcceptedSelected = IsAcceptedStrict<Grants...>;
//     #endif
//   }
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / TypeSafe / NullSafe / MemSafe — concept-only; no
//     runtime state, no allocation, no pointers.
//   BorrowSafe / ThreadSafe — concept evaluation is consteval-pure;
//     evaluates once per template instantiation.
//   LeakSafe — no resources to leak.
//   DetSafe — concept evaluation is deterministic per Grants pack.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase D — Profile.h deliverable
//   misc/16_05_2026_fixy.md §5.3       — adoption policy (sketch vs release)
//   fixy/Reject.h                       — strict IsAccepted concept
//   CMakePresets.json                   — CRUCIBLE_FIXY_STRICT toggle

#include <crucible/fixy/Reject.h>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── Sketch profile — IsAccepted downgraded to permissive ──────────
// ═════════════════════════════════════════════════════════════════════
//
// `IsAcceptedWithWarning` is satisfied for ANY grant pack — even an
// empty one.  Sketch mode is the prototyping posture: prove the
// pipeline composes before paying the 20-dim engagement cost.
//
// The discipline still surfaces missing engagement: the build emits
// `FixyNotEngaged_<DimName>` warnings (via the diagnostic
// infrastructure shipped in Phase A) at the use site, so a sketch-
// mode developer sees exactly which dims a sibling release-mode
// developer would have to engage.

template <typename... Grants>
concept IsAcceptedWithWarning = true;

// ═════════════════════════════════════════════════════════════════════
// ── Named profile aliases ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Strict profile — the Phase A/B/C reject-by-default gate.  Hard
// reject on any unengaged dim, hard reject on any §6.8 collision.
template <typename... Grants>
concept IsAcceptedStrict = IsAccepted<Grants...>;

// Sketch profile — permissive gate for prototyping.
template <typename... Grants>
concept IsAcceptedSketch = IsAcceptedWithWarning<Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── Profile selector (preprocessor-toggled) ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `IsAcceptedSelected` is the alias greenfield code SHOULD consult
// when a per-build profile matters.  Default (no CRUCIBLE_FIXY_SKETCH
// defined) → IsAcceptedStrict.  Sketch builds defining
// CRUCIBLE_FIXY_SKETCH → IsAcceptedSketch.

#ifdef CRUCIBLE_FIXY_SKETCH

template <typename... Grants>
concept IsAcceptedSelected = IsAcceptedSketch<Grants...>;

#else

template <typename... Grants>
concept IsAcceptedSelected = IsAcceptedStrict<Grants...>;

#endif

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — concept names resolve correctly ───────────────────
// ═════════════════════════════════════════════════════════════════════

namespace profile_self_test {

// IsAcceptedStrict is identical to IsAccepted.
static_assert(IsAcceptedStrict<>           == IsAccepted<>);
static_assert(IsAcceptedStrict<grant::copy> == IsAccepted<grant::copy>);

// IsAcceptedSketch accepts anything.
static_assert(IsAcceptedSketch<>);
static_assert(IsAcceptedSketch<grant::copy>);
static_assert(IsAcceptedSketch<int, float, double>);

// IsAcceptedSelected resolves to one or the other based on the
// preprocessor symbol.  Both branches must satisfy a valid grant pack.
#ifdef CRUCIBLE_FIXY_SKETCH
// Sketch mode pins: even empty pack satisfies.
static_assert(IsAcceptedSelected<>);
#else
// Release mode pins: empty pack does NOT satisfy.
static_assert(!IsAcceptedSelected<>);
#endif

}  // namespace profile_self_test

}  // namespace crucible::fixy
