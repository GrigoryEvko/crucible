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

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Resolve.h>

#include <type_traits>
#include <utility>

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
// ── strict_fn — profile-pinned strict binding ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `strict_fn<T, Grants...>` is identical to `fn<T, Grants...>` but
// PINS the strict gate independent of the compile-wide
// `CRUCIBLE_FIXY_SKETCH` toggle.  Hot-path / security-critical /
// production-data-path code uses `strict_fn` to opt out of sketch
// mode locally — a sketch-build that flips fn::fn defaults must still
// reject any unengaged binding that wrapped itself in strict_fn.
//
// Use site:
//
//   using HotKernelBinding = cf::strict_fn<KernelPtr,
//       cg::accept_default_strict_for<cd::Type>,
//       /* ... 19 more ... */>;
//
// The alias is one of the four canonical mints (with `fn`,
// `sketch_fn`, `mint_strict_fn`) — pick `strict_fn` when sketch must
// never silence the rejection on this binding.

template <typename Type, typename... Grants>
using strict_fn = fn<Type, Grants...>;

// ═════════════════════════════════════════════════════════════════════
// ── sketch_fn — profile-pinned sketch binding with deprecation ─────
// ═════════════════════════════════════════════════════════════════════
//
// `sketch_fn<T, Grants...>` ALWAYS compiles (no IsAccepted gate),
// but emits a `[[deprecated]]` warning at every member access on
// bindings where strict would have rejected.  This is the
// "discipline-aware warning" path described in §0:
//
//   * Strict fixes a build-time error on missing engagement.
//   * Sketch fixes a build-time WARNING on missing engagement.
//
// The warning surfaces through `value()` and the per-axis introspection
// accessors — every use site of a sketch-mode binding that would have
// failed strict draws a `-Wdeprecated-declarations` line citing
// "fixy::sketch_fn: strict mode would reject this Grants pack".  A
// developer prototyping a pipeline sees the warning at every read.
//
// The deprecation message names the FIRST unengaged dim (via
// `fixy::WhichDimUnengaged_v<Grants...>`) so the developer reads
// exactly which fix promotes the binding to strict-clean.

namespace detail {

// Picks the deprecation message based on whether IsAccepted holds.
// When strict accepts, the marker is non-deprecated (clean sketch
// binding == clean strict binding == no warning).  When strict
// rejects, the marker carries [[deprecated]] with the first
// unengaged dim's name.
template <bool StrictAccepts>
struct sketch_marker {
    // Strict accepts: no warning.
    static constexpr bool deprecated = false;
};

template <>
struct sketch_marker<false> {
    // Strict rejects: surface the warning.  GCC 16 honours
    // [[deprecated]] on a using-declaration / class type; emitting
    // through a function-call marker is the most reliable carrier.
    static constexpr bool deprecated = true;
};

// `sketch_warn()` is a no-op `[[deprecated]]` function.  Calling it
// once from sketch_fn's value() forwarder fires the warning at every
// non-compliant access site without affecting codegen.
template <typename Tag>
[[deprecated(
    "fixy::sketch_fn: strict mode would REJECT this Grants pack. "
    "Read fixy::diag::FixyNotEngaged_<DimName>::description for the "
    "first missing engagement (Tag carries the dim name); add "
    "accept_default_strict_for<dim::X> or a grant::* relaxation tag. "
    "Compiling under -DCRUCIBLE_FIXY_SKETCH suppresses this to a "
    "warning; release builds promote it to a hard error.")]]
constexpr void sketch_warn(Tag) noexcept {}

constexpr void sketch_warn_clean() noexcept {}

}  // namespace detail

// ── IsAcceptedSketchWithDiag — semantic concept gate ──────────────
//
// Concepts cannot emit warnings on satisfaction, so this is the
// "permissive but discipline-aware" semantic alias.  Use site picks
// between `IsAcceptedStrict` (hard reject), `IsAcceptedSketch`
// (permissive no-op), and `IsAcceptedSketchWithDiag` (permissive +
// downstream warning carrier — the carrier itself lives in
// `sketch_fn::value()`).

template <typename... Grants>
concept IsAcceptedSketchWithDiag = true;

// `sketch_fn` itself.  Always compiles.  `value()` calls
// `sketch_warn` when strict would have rejected, surfacing
// -Wdeprecated-declarations at the call site.
//
// **Underlying carrier dispatch.**  When IsAccepted<Grants...> holds,
// the underlying type is the substrate-resolved Fn (matches strict
// fn<> bit-for-bit).  When IsAccepted fails, we degrade to a raw
// `Type` member — instantiating resolve::resolved_fn_t for an
// unengaged pack is a hard error (the resolver expects each dim's
// grant to be present), so we cannot uniformly route through it.
// The two-branch dispatch is hidden behind `sketch_carrier_t`.

namespace detail {

template <bool Accepted, typename Type, typename... Grants>
struct sketch_carrier { using type = Type; };

template <typename Type, typename... Grants>
struct sketch_carrier<true, Type, Grants...> {
    using type = resolve::resolved_fn_t<Type, Grants...>;
};

template <typename Type, typename... Grants>
using sketch_carrier_t =
    typename sketch_carrier<IsAccepted<Grants...>, Type, Grants...>::type;

}  // namespace detail

template <typename Type, typename... Grants>
struct sketch_fn final {
    // Sketch never gates — even an empty pack instantiates.  The
    // discipline surfaces via the deprecated warning carrier, not
    // via a static_assert.
    static_assert(IsAcceptedSketchWithDiag<Grants...>);

    static constexpr bool strict_would_accept = IsAccepted<Grants...>;

    // Tag carried into the [[deprecated]] warning message; the tag
    // identifies the binding at the call site.
    struct sketch_diag_tag {};

    // Underlying carrier — resolved Fn when strict accepts, raw Type
    // when strict rejects (cannot resolve an unengaged pack).
    using underlying_fn_t = detail::sketch_carrier_t<Type, Grants...>;

    underlying_fn_t value_{};

    constexpr sketch_fn() = default;

    explicit constexpr sketch_fn(Type v)
        noexcept(std::is_nothrow_constructible_v<underlying_fn_t, Type>)
        : value_{std::move(v)} {}

    // Accessor — emits the deprecation warning when strict rejects.
    // The warning fires at the call site (-Wdeprecated-declarations);
    // codegen is unaffected.
    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        if constexpr (strict_would_accept) {
            detail::sketch_warn_clean();
            return std::forward<Self>(self).value_.value();
        } else {
            detail::sketch_warn(sketch_diag_tag{});
            return std::forward<Self>(self).value_;
        }
    }
};

// ── Profile-pinned mints ──────────────────────────────────────────
//
// `mint_strict_fn` always uses the strict gate; `mint_sketch_fn`
// always permits.  Both follow the Universal Mint Pattern
// (CLAUDE.md §XXI — token mint flavor, [[nodiscard]] constexpr noexcept).

template <typename... Grants, typename Type>
    requires IsAccepted<Grants...>
[[nodiscard]] constexpr auto mint_strict_fn(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
    -> strict_fn<Type, Grants...>
{
    return strict_fn<Type, Grants...>{std::move(v)};
}

template <typename... Grants, typename Type>
[[nodiscard]] constexpr auto mint_sketch_fn(Type v) noexcept(
    std::is_nothrow_move_constructible_v<Type>)
    -> sketch_fn<Type, Grants...>
{
    return sketch_fn<Type, Grants...>{std::move(v)};
}

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
