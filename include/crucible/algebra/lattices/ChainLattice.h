#pragma once

// ── crucible::algebra::lattices::ChainLatticeOps<EnumT> ─────────────
//
// Helper base class extracting the leq/join/meet ops shared by every
// chain-order lattice over a strong enum.  Three current callers —
// LifetimeLattice (3 tiers), ConsistencyLattice (5 tiers),
// ToleranceLattice (7 tiers) — independently re-implemented these
// three identical static methods; this base centralizes them.
//
// ── What it provides ────────────────────────────────────────────────
//
// For any `EnumT` satisfying `std::is_enum_v<EnumT>`, exposes:
//
//   using element_type = EnumT
//   static constexpr bool   leq(EnumT a, EnumT b)   noexcept
//   static constexpr EnumT  join(EnumT a, EnumT b)  noexcept   (max)
//   static constexpr EnumT  meet(EnumT a, EnumT b)  noexcept   (min)
//
// Implementation pattern: cast to the underlying integer via
// std::to_underlying, then compare.  Identical bytes across every
// per-tier chain lattice.
//
// ── What it does NOT provide ────────────────────────────────────────
//
// Per-lattice-specific bottom() / top() / name() and per-tier At<L>
// nested templates remain in the derived lattice struct — these
// vary per enum (different bottom/top values, different name strings,
// different At<L>::name() switch arms) and can't be sensibly factored.
//
// The expected per-lattice header pattern is:
//
//   struct LifetimeLattice : ChainLatticeOps<Lifetime> {
//       [[nodiscard]] static constexpr Lifetime bottom() noexcept {
//           return Lifetime::PER_REQUEST;
//       }
//       [[nodiscard]] static constexpr Lifetime top() noexcept {
//           return Lifetime::PER_FLEET;
//       }
//       [[nodiscard]] static consteval std::string_view name() noexcept {
//           return "LifetimeLattice";
//       }
//
//       template <Lifetime L>
//       struct At { ...singleton sub-lattice... };
//   };
//
// ── Why static-method inheritance, not template parameterization ────
//
// We extract the OPS into a base, not the WHOLE lattice into a
// `template <typename EnumT, ...> ChainLattice` template, for three
// reasons:
//
//   1. Each per-lattice struct STAYS A DISTINCT TYPE.  The
//      neg-compile cross-lattice tests (Lifetime × Consistency
//      etc.) depend on the per-lattice enum being structurally
//      different; if we used a template alias, the wrapper types
//      could end up sharing identity in surprising ways.
//   2. Public API surface is UNCHANGED.  Callers still write
//      `LifetimeLattice::leq(...)` and `LifetimeLattice::At<L>`
//      exactly as before — qualified-name lookup walks the base.
//   3. Concept gates (`Lattice<LifetimeLattice>`) still satisfy via
//      static-method inheritance: the concept's `L::leq(a, b)` probe
//      finds the inherited method through normal name lookup.
//
//   Axiom coverage:
//     TypeSafe — strong-enum constraint (std::is_enum_v) prevents
//                accidental instantiation over ints/floats.
//     DetSafe  — every operation is `constexpr` (NOT `consteval`)
//                so Graded's runtime `pre (L::leq(...))` precondition
//                can fire under enforce semantic.
//   Runtime cost: zero — static methods, EBO base (sizeof empty base
//                  collapses).
//
// See ALGEBRA-14 (#459, Lifetime/Consistency/Tolerance lattices) for
// the three callers; the audit Tier-2 sweep (this commit) for the
// extraction motivation.

#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

template <typename EnumT>
    requires std::is_enum_v<EnumT>
struct ChainLatticeOps {
    using element_type = EnumT;

    [[nodiscard]] static constexpr bool leq(EnumT a, EnumT b) noexcept {
        return std::to_underlying(a) <= std::to_underlying(b);
    }
    [[nodiscard]] static constexpr EnumT join(EnumT a, EnumT b) noexcept {
        return leq(a, b) ? b : a;  // max — strengthen
    }
    [[nodiscard]] static constexpr EnumT meet(EnumT a, EnumT b) noexcept {
        return leq(a, b) ? a : b;  // min — weaken
    }
};

}  // namespace crucible::algebra::lattices
