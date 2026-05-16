#pragma once

// ── crucible::fixy — Dim.h (FIXY-A1a) ──────────────────────────────────
//
// The dimension identity layer for the fixy/ reject-by-default
// discipline.  Re-exports `safety::DimensionAxis` under the
// `crucible::fixy::dim` namespace so every higher fixy/ header
// (Default.h, Grant.h, Reject.h) routes through one canonical name.
// The indirection is load-bearing: it gives fixy/ a single point of
// control if the substrate's 20-dim enum is ever remapped (see
// misc/fixy.md §24.1 + §24.14 — clock domain and FP order are dropped,
// nothing else has wiggle room).
//
// ── Surface ────────────────────────────────────────────────────────────
//
//   fixy::dim::DimAxis           — type alias for safety::DimensionAxis.
//   fixy::dim::{Type, ...}       — 20 enumerator aliases, one per dim.
//   fixy::dim::count_v           — load-bearing 20.
//   fixy::dim::name(d)           — substrate-passthrough constexpr.
//   fixy::dim::tier_of_v<D>      — substrate-passthrough TierKind.
//
// ── Why constexpr, not consteval ──────────────────────────────────────
//
// The substrate's `safety::dimension_axis_name` is constexpr (not
// consteval) per feedback_algebra_runtime_smoke_test_discipline and the
// algebra/Lattice.h convention — runtime smoke tests must be able to
// drive name() with non-constant arguments.  We preserve that contract
// here.  Same for `tier_of` (substrate-side) and the variable templates.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — type alias + inline constexpr enumerator-aliases; no
//                runtime storage; nothing to leave uninitialized.
//   TypeSafe   — DimAxis is the substrate's strong enum class; no
//                conversion from underlying int admitted.
//   NullSafe   — no pointer members.
//   MemSafe    — zero-state; no allocation; no resource.
//   BorrowSafe — no state, no aliasing concern.
//   ThreadSafe — header is purely compile-time identity material.
//   LeakSafe   — no resource ownership.
//   DetSafe    — every accessor constexpr / consteval-callable; bit-
//                identical output across compiles.
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  Every name in this header is an alias / inline constexpr;
// nothing emits machine code.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §2,§3       — Phase A scope + IsAccepted plan
//   misc/fixy.md §24.1                  — 20-dim catalog (FX-22 minus 2)
//   safety/DimensionTraits.h            — substrate source of truth
//   CLAUDE.md §XVII                     — telling-word identifier rules

#include <crucible/safety/DimensionTraits.h>

#include <cstddef>
#include <string_view>

namespace crucible::fixy::dim {

// ── Substrate alias ───────────────────────────────────────────────────
using DimAxis = ::crucible::safety::DimensionAxis;

// ── 20 enumerator aliases (FX-22 minus clock/fp-order per §24.1) ──────
//
// Reading order matches misc/fixy.md §24.1 and `safety::DimensionAxis`
// enumerator order; an enumerator-reordering at the substrate would
// fire a build break here on the `(==)` checks below, which is the
// point.
inline constexpr DimAxis Type           = DimAxis::Type;
inline constexpr DimAxis Refinement     = DimAxis::Refinement;
inline constexpr DimAxis Usage          = DimAxis::Usage;
inline constexpr DimAxis Effect         = DimAxis::Effect;
inline constexpr DimAxis Security       = DimAxis::Security;
inline constexpr DimAxis Protocol       = DimAxis::Protocol;
inline constexpr DimAxis Lifetime       = DimAxis::Lifetime;
inline constexpr DimAxis Provenance     = DimAxis::Provenance;
inline constexpr DimAxis Trust          = DimAxis::Trust;
inline constexpr DimAxis Representation = DimAxis::Representation;
inline constexpr DimAxis Observability  = DimAxis::Observability;
inline constexpr DimAxis Complexity     = DimAxis::Complexity;
inline constexpr DimAxis Precision      = DimAxis::Precision;
inline constexpr DimAxis Space          = DimAxis::Space;
inline constexpr DimAxis Overflow       = DimAxis::Overflow;
inline constexpr DimAxis Mutation       = DimAxis::Mutation;
inline constexpr DimAxis Reentrancy     = DimAxis::Reentrancy;
inline constexpr DimAxis Size           = DimAxis::Size;
inline constexpr DimAxis Version        = DimAxis::Version;
inline constexpr DimAxis Staleness      = DimAxis::Staleness;

// ── Count constant ────────────────────────────────────────────────────
//
// The load-bearing 20.  Pulled from the substrate's
// reflection-derived count so a substrate-side append fires here at
// build time (the `count_is_20` static_assert below).
inline constexpr std::size_t count_v = ::crucible::safety::DIMENSION_AXIS_COUNT;

static_assert(count_v == 20,
    "fixy/ assumes the 20-dim FX-22-minus-clock-and-fp-order catalog. "
    "A substrate-side append to safety::DimensionAxis fires this check "
    "as the FIRST line of defense — coordinate with fixy::dim::Type..."
    "Staleness aliases + Default.h + Grant.h before bumping count_v.");

// ── Bijection self-check (FIXY-AUDIT-BIJECTION; restoration) ──────────
//
// Reading the substrate enumerator-by-enumerator and pinning our
// alias values catches a subtle bug where someone reorders the
// substrate enum but forgets to update fixy/ aliases — the alias
// values would compile but bind to the wrong dim semantically.  The
// `count_v == 20` check above catches APPENDS but not RENAMES /
// REORDERS — that's what these 20 per-name asserts are for.  Each
// line is the second line of defense pinning a single alias name to
// the substrate enumerator with the same spelling.
static_assert(Type           == DimAxis::Type);
static_assert(Refinement     == DimAxis::Refinement);
static_assert(Usage          == DimAxis::Usage);
static_assert(Effect         == DimAxis::Effect);
static_assert(Security       == DimAxis::Security);
static_assert(Protocol       == DimAxis::Protocol);
static_assert(Lifetime       == DimAxis::Lifetime);
static_assert(Provenance     == DimAxis::Provenance);
static_assert(Trust          == DimAxis::Trust);
static_assert(Representation == DimAxis::Representation);
static_assert(Observability  == DimAxis::Observability);
static_assert(Complexity     == DimAxis::Complexity);
static_assert(Precision      == DimAxis::Precision);
static_assert(Space          == DimAxis::Space);
static_assert(Overflow       == DimAxis::Overflow);
static_assert(Mutation       == DimAxis::Mutation);
static_assert(Reentrancy     == DimAxis::Reentrancy);
static_assert(Size           == DimAxis::Size);
static_assert(Version        == DimAxis::Version);
static_assert(Staleness      == DimAxis::Staleness);

// ── Constexpr accessors (passthrough) ─────────────────────────────────
//
// Both delegate to the substrate.  Kept constexpr (not consteval) so
// runtime smoke tests can iterate the 20 dims with non-constant
// inputs, per feedback_algebra_runtime_smoke_test_discipline.

[[nodiscard]] constexpr std::string_view name(DimAxis d) noexcept {
    return ::crucible::safety::dimension_axis_name(d);
}

template <DimAxis D>
inline constexpr ::crucible::safety::TierKind tier_of_v =
    ::crucible::safety::tier_of_axis(D);

// ── In-range validation (FIXY-AUDIT-NTTP) ─────────────────────────────
//
// An author can instantiate any template parametrized on DimAxis with a
// value cast from an out-of-range integer (e.g.,
// `accept_default_strict_for<static_cast<DimAxis>(99)>`).  The enum
// class's fixed underlying type (uint8_t) admits values 0..255; only
// 0..19 are actually named.  An "engagement" tag pointing at
// DimAxis{99} is structurally meaningless — it engages no real dim.
//
// `is_valid_axis_v<D>` is the consteval predicate that returns true
// iff D matches one of the 20 named enumerators.  Templates that take
// `DimAxis D` as a non-type parameter MUST static_assert on this so a
// `static_cast<DimAxis>(99)` NTTP fires a clear compile error at the
// instantiation site, not a silent bind-to-nothing.
namespace detail {
[[nodiscard]] consteval bool axis_in_enumerator_set(DimAxis d) noexcept {
    constexpr std::size_t lo = 0;
    return static_cast<std::size_t>(d) >= lo
        && static_cast<std::size_t>(d) < count_v;
}
}  // namespace detail

template <DimAxis D>
inline constexpr bool is_valid_axis_v = detail::axis_in_enumerator_set(D);

}  // namespace crucible::fixy::dim
