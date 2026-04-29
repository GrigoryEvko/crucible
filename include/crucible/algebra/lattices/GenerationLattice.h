#pragma once

// ── crucible::algebra::lattices::GenerationLattice ──────────────────
//
// Bounded total-order lattice over a uint64_t MONOTONIC counter
// representing the per-Relay GENERATION — the local restart counter
// each Relay maintains independently.  Sister axis to Epoch; the
// second component sub-lattice for the EpochVersioned product
// wrapper from 28_04_2026_effects.md §4.4.2 (FOUND-G67).
//
// Citation: CRUCIBLE.md §L13 (per-Relay generation alongside fleet
// epoch); §L14 (Cipher reincarnation across Relay restarts).
//
// THE LOAD-BEARING USE CASE: Relay restart bookkeeping.  Each
// Relay's generation counter advances on every fresh-start (Keeper
// daemon restart, Cipher reload, hardware reseat).  A value tagged
// with (epoch=5, generation=2) was produced by the cluster at
// epoch 5 by a Relay on its second incarnation.  The fleet epoch
// is global; the generation is local to each Relay.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier:  Generation = strong-typed uint64_t.
// Order:    natural ≤ on uint64_t.
// Bottom:   Generation{0}             (initial generation — Relay
//                                      first boot.)
// Top:      Generation{UINT64_MAX}    (saturating cap.)
// Join:     max                       (the MORE RECENT generation.)
// Meet:     min                       (the older generation.)
//
// ── Direction convention ────────────────────────────────────────────
//
// Same as EpochLattice and the Budgeted axes: ordered by NUMERIC ≤,
// NOT by claim strength.  See EpochLattice.h for the rationale and
// the spec citation.  Both EpochVersioned axes share this direction
// so that the binary product `ProductLattice<EpochLattice,
// GenerationLattice>` has a coherent pointwise ordering.
//
// ── Why this is a DIFFERENT type from Epoch ────────────────────────
//
// Generation and Epoch are both `uint64_t`-backed lattices over the
// natural-≤ order.  Without strong typing, an axis swap at any
// EpochVersioned construction site (passing a Generation where an
// Epoch was expected, or vice versa) would silently compile, and
// downstream Canopy reshard-validation gates would compare
// generations against the fleet epoch — semantically wrong.
//
//   Axiom coverage:
//     TypeSafe — Generation is structurally identical to Epoch
//                under the hood (both wrap uint64_t) but carries a
//                distinct phantom identity.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = Generation = uint64_t + 0 phantom bytes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── Generation — strong-typed uint64_t per-Relay restart counter ──
struct Generation {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(Generation const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(Generation const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

// ── GenerationLattice — bounded chain over Generation ─────────────
struct GenerationLattice {
    using element_type = Generation;

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
        return "GenerationLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::generation_lattice_self_test {

static_assert(Lattice<GenerationLattice>);
static_assert(BoundedLattice<GenerationLattice>);
static_assert(!UnboundedLattice<GenerationLattice>);
static_assert(!Semiring<GenerationLattice>);

static_assert(sizeof(Generation) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Generation>);
static_assert(std::is_standard_layout_v<Generation>);

static_assert(!std::is_same_v<Generation, std::uint64_t>);

// Strong typing: Generation is NOT structurally the same type as
// Epoch (the sister axis) even though both wrap uint64_t.  This is
// the load-bearing identity for EpochVersioned's axis discipline.

// Ordering witnesses.
static_assert( GenerationLattice::leq(Generation{0},   Generation{1024}));
static_assert( GenerationLattice::leq(Generation{42},  Generation{42}));
static_assert(!GenerationLattice::leq(Generation{2048}, Generation{1024}));

// Bounds.
static_assert(GenerationLattice::bottom().value == 0);
static_assert(GenerationLattice::top().value    == std::numeric_limits<std::uint64_t>::max());

// Join / meet.
static_assert(GenerationLattice::join(Generation{1}, Generation{5}).value == 5);
static_assert(GenerationLattice::join(Generation{5}, Generation{1}).value == 5);
static_assert(GenerationLattice::meet(Generation{1}, Generation{5}).value == 1);

// Bound identities.
static_assert(GenerationLattice::join(Generation{7}, GenerationLattice::bottom())
              == Generation{7});
static_assert(GenerationLattice::meet(Generation{7}, GenerationLattice::top())
              == Generation{7});

// Idempotence.
static_assert(GenerationLattice::join(Generation{99}, Generation{99}).value == 99);
static_assert(GenerationLattice::meet(Generation{99}, Generation{99}).value == 99);

// Distributivity witness.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    Generation a{1};
    Generation b{4};
    Generation c{16};
    auto       lhs = GenerationLattice::meet(a, GenerationLattice::join(b, c));
    auto       rhs = GenerationLattice::join(GenerationLattice::meet(a, b),
                                             GenerationLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Implicit conversion DOWN to uint64_t.
static_assert([] consteval {
    Generation    g{42};
    std::uint64_t n = g;
    return n == 42;
}());

inline void runtime_smoke_test() {
    Generation                    bot   = GenerationLattice::bottom();
    Generation                    topv  = GenerationLattice::top();
    Generation                    mid   {7};
    [[maybe_unused]] bool         l     = GenerationLattice::leq(bot, topv);
    [[maybe_unused]] Generation   j     = GenerationLattice::join(mid, topv);
    [[maybe_unused]] Generation   m     = GenerationLattice::meet(mid, bot);

    // Per-Relay restart progression.
    Generation                    g_initial{0};
    Generation                    g_after_first_restart{1};
    Generation                    g_after_second_restart{2};
    Generation                    most_recent =
        GenerationLattice::join(g_initial,
            GenerationLattice::join(g_after_first_restart, g_after_second_restart));
    if (most_recent.value != 2u) std::abort();

    std::uint64_t                 total = mid;
    if (total != 7u) std::abort();

    using GenerationGraded = Graded<ModalityKind::Absolute, GenerationLattice, double>;
    GenerationGraded              v{3.14, Generation{8}};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::generation_lattice_self_test

}  // namespace crucible::algebra::lattices
