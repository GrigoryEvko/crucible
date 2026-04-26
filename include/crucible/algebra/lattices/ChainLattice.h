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

#include <crucible/algebra/Lattice.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Constraint: SCOPED enum, not plain ──────────────────────────────
//
// std::is_scoped_enum_v (C++23) accepts only `enum class : T { ... }`
// declarations — strong enums that don't implicitly convert to their
// underlying integer type.  Plain `enum E : int { ... }` is rejected
// at the concept gate (defeats Crucible's strong-enum discipline:
// implicit-int-convertibility lets `if (lifetime + 1) ...` compile,
// which is precisely what the strong enum was meant to forbid).
template <typename EnumT>
    requires std::is_scoped_enum_v<EnumT>
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

// ── Exhaustive verifiers — one consteval helper per check ──────────
//
// Each chain lattice's self-test independently re-implemented the
// triple-nested `template for (constexpr auto e : enumerators)` fold
// over the lattice's enum, calling either verify_bounded_lattice_
// axioms_at or verify_distributive_lattice for every (a, b, c) triple.
// Audit Tier-2 dedup: factor those two helpers here, parameterized
// over the ChainLattice itself.
//
// The reflection-driven enumeration (P2996R13 + P3491R3
// define_static_array) auto-extends coverage when the underlying
// enum gains a new variant — no per-lattice update needed.

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^EnumT));
    // -Wshadow fires on `template for` bodies because GCC 16 unrolls
    // the loop into successive scopes that each declare the same
    // induction variable; suppress locally for the loop body only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_bounded_lattice_axioms_at<ChainLattice>(
                        [:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename ChainLattice>
[[nodiscard]] consteval bool verify_chain_lattice_distributive_exhaustive() noexcept {
    using EnumT = typename ChainLattice::element_type;
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^EnumT));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            template for (constexpr auto ec : enumerators) {
                if (!verify_distributive_lattice<ChainLattice>(
                        [:ea:], [:eb:], [:ec:])) {
                    return false;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace crucible::algebra::lattices
