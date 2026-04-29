#pragma once

// ── crucible::algebra::lattices::EpochLattice ───────────────────────
//
// Bounded total-order lattice over a uint64_t MONOTONIC counter
// representing the Canopy fleet epoch — the cluster-wide Raft-
// committed membership generation.  One of two component sub-
// lattices for the EpochVersioned product wrapper from
// 28_04_2026_effects.md §4.4.2 (FOUND-G67).
//
// Citation: CRUCIBLE.md §L13 (Canopy Raft-committed membership
// epoch); §L14 (Cipher reincarnation across topology changes).
//
// THE LOAD-BEARING USE CASE: every Canopy collective; every
// reshard event.  The Canopy mesh advances its fleet epoch ONLY
// via Raft-committed membership changes (peer join, peer eviction,
// reshard).  A value tagged with epoch=5 is admissible at any
// gate requiring epoch ≥ 5; tagging at older epochs is rejected
// (because the cluster's view of "who participates" has changed).
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier:  Epoch = strong-typed uint64_t.
// Order:    natural ≤ on uint64_t.
// Bottom:   Epoch{0}             (genesis epoch — fleet initial state.)
// Top:      Epoch{UINT64_MAX}    (saturating cap.)
// Join:     max                  (the MORE RECENT of two views.)
// Meet:     min                  (the older of two views.)
//
// ── Direction convention ────────────────────────────────────────────
//
// Same as the Budgeted axes' BitsBudget / PeakBytes (FOUND-G63):
// ordered by NUMERIC ≤, NOT by claim strength.  Rationale:
//
//   - The grade carries an ACTUAL committed-epoch number, not a
//     claim CAP.  Composing two values' epochs takes the MAX
//     (the value with more-recent membership view subsumes the
//     older one).
//
//   - Admission gates downstream read "is this value at least at
//     epoch N?" — admission requires source.epoch ≥ gate.  With
//     leq = ≤, the natural reading is "old ⊑ new" — older epochs
//     are below newer ones in the lattice.
//
// THE FORWARD-PROGRESS DISCIPLINE: monotone counters NEVER regress.
// A reshard ADVANCES the epoch; a peer joining ADVANCES the epoch.
// The lattice does NOT enforce this at the type level (you can
// construct Epoch{3} after constructing Epoch{5} — the lattice
// only knows about ordering, not history); the wrapper's
// production call sites must enforce the forward-progress rule
// at construction sites (typically by deriving the new epoch from
// the Raft commit log, not from arbitrary inputs).
//
//   Axiom coverage:
//     TypeSafe — Epoch is a strong-tagged uint64_t; mixing with
//                Generation (the sister axis) is a compile error.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = Epoch = uint64_t + 0 phantom bytes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── Epoch — strong-typed uint64_t fleet-epoch counter ─────────────
//
// Phantom-typed wrapper around uint64_t.  Distinct from Generation
// (the sister axis) AND distinct from BitsBudget / PeakBytes (the
// Budgeted axes) — all four are uint64_t-backed but each is a
// distinct C++ struct, so cross-axis assignment is a compile error.
struct Epoch {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(Epoch const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(Epoch const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

// ── EpochLattice — bounded chain over Epoch ────────────────────────
struct EpochLattice {
    using element_type = Epoch;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{0};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return a.value <= b.value;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value >= b.value ? a.value : b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value <= b.value ? a.value : b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "EpochLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::epoch_lattice_self_test {

static_assert(Lattice<EpochLattice>);
static_assert(BoundedLattice<EpochLattice>);
static_assert(!UnboundedLattice<EpochLattice>);
static_assert(!Semiring<EpochLattice>);

static_assert(sizeof(Epoch) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Epoch>);
static_assert(std::is_standard_layout_v<Epoch>);

static_assert(!std::is_same_v<Epoch, std::uint64_t>);

// Ordering witnesses.
static_assert( EpochLattice::leq(Epoch{0},   Epoch{1}));
static_assert( EpochLattice::leq(Epoch{42},  Epoch{42}));   // reflexive
static_assert(!EpochLattice::leq(Epoch{99},  Epoch{42}));
static_assert( EpochLattice::leq(EpochLattice::bottom(), EpochLattice::top()));

// Bounds.
static_assert(EpochLattice::bottom().value == 0);
static_assert(EpochLattice::top().value    == std::numeric_limits<std::uint64_t>::max());

// Join / meet.
static_assert(EpochLattice::join(Epoch{3}, Epoch{7}).value == 7);
static_assert(EpochLattice::join(Epoch{7}, Epoch{3}).value == 7);   // commutative
static_assert(EpochLattice::meet(Epoch{3}, Epoch{7}).value == 3);

// Bound identities.
static_assert(EpochLattice::join(Epoch{42}, EpochLattice::bottom()) == Epoch{42});
static_assert(EpochLattice::meet(Epoch{42}, EpochLattice::top())    == Epoch{42});

// Idempotence.
static_assert(EpochLattice::join(Epoch{99}, Epoch{99}).value == 99);
static_assert(EpochLattice::meet(Epoch{99}, Epoch{99}).value == 99);

// Distributivity witness.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    Epoch a{2};
    Epoch b{5};
    Epoch c{8};
    auto  lhs = EpochLattice::meet(a, EpochLattice::join(b, c));
    auto  rhs = EpochLattice::join(EpochLattice::meet(a, b),
                                   EpochLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Implicit conversion DOWN to uint64_t.
static_assert([] consteval {
    Epoch         e{42};
    std::uint64_t n = e;
    return n == 42;
}());

inline void runtime_smoke_test() {
    Epoch                         bot   = EpochLattice::bottom();
    Epoch                         topv  = EpochLattice::top();
    Epoch                         mid   {1024};
    [[maybe_unused]] bool         l     = EpochLattice::leq(bot, topv);
    [[maybe_unused]] Epoch        j     = EpochLattice::join(mid, topv);
    [[maybe_unused]] Epoch        m     = EpochLattice::meet(mid, bot);

    // Forward progression: each Raft commit advances the epoch.
    Epoch                         e_at_genesis{0};
    Epoch                         e_after_join{1};
    Epoch                         e_after_reshard{2};
    Epoch                         most_recent =
        EpochLattice::join(EpochLattice::join(e_at_genesis, e_after_join),
                           e_after_reshard);
    if (most_recent.value != 2u) std::abort();

    // Implicit unwrap.
    std::uint64_t                 total = mid;
    if (total != 1024u) std::abort();

    // Lattice over Graded substrate.
    using EpochGraded = Graded<ModalityKind::Absolute, EpochLattice, int>;
    EpochGraded                   v{42, Epoch{16}};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::epoch_lattice_self_test

}  // namespace crucible::algebra::lattices
