#pragma once

// ── crucible::algebra::lattices::AffinityLattice ────────────────────
//
// BOOLEAN-ALGEBRA lattice over a CPU affinity bitmask — distinct
// algebraic structure from the chain lattices (BitsBudget, Epoch,
// etc.) and partial-order lattices (Vendor, NumaNodeLattice).  A
// powerset lattice over the universe of CPU core IDs (0..63 in this
// 64-bit-mask incarnation; future-extending to 256+ would require
// a multi-word mask).
//
// Sister axis to NumaNodeLattice; the second component sub-lattice
// for the NumaPlacement product wrapper from 28_04_2026_effects.md
// §4.4.3 (FOUND-G71).
//
// Citation: THREADING.md §5.4 (cache-tier rule with NUMA-local
// placement); CRUCIBLE.md §L13 (NUMA-aware ThreadPool worker
// affinity).
//
// THE LOAD-BEARING USE CASE: AdaptiveScheduler placement.  A value
// tagged with AffinityMask{0b00111100} = "cores 2..5" admits being
// scheduled on any of those cores.  A scheduler asking "can this
// task run on core N?" admits iff the value's affinity mask has
// bit N set.
//
// ── Algebraic shape (boolean lattice) ───────────────────────────────
//
// Carrier:  AffinityMask = strong-typed uint64_t (bit i = core i).
// Order:    leq(a, b) = a ⊆ b = (a & b) == a   (set inclusion)
// Bottom:   AffinityMask{0}            (empty set — admits no core)
// Top:      AffinityMask{~0ULL}        (all 64 cores — wildcard)
// Join:     a | b                      (union of cores)
// Meet:     a & b                      (intersection of cores)
//
// This is a DISTRIBUTIVE BOOLEAN LATTICE — every element has a
// complement (the bitwise NOT), satisfies de Morgan's laws, and
// has 2^64 elements.  Distributivity holds at every triple
// (a ∧ (b ∨ c)) == ((a ∧ b) ∨ (a ∧ c)) by the distributivity of
// AND over OR on bits.
//
// ── Direction convention ────────────────────────────────────────────
//
// Bigger set = HIGHER in the lattice = more permissive.  Same as
// the four sister uint64-backed lattices (BitsBudget, PeakBytes,
// Epoch, Generation): numeric ≤ on the bitmask.  Top = wildcard
// (admits everywhere), bottom = pinned-to-nothing.
//
//   - Bottom = empty mask = "this value admits NO core" = WEAKEST
//     claim about admission (the value can't run anywhere).
//   - Top = all-cores mask = "this value admits ANY core" =
//     STRONGEST claim (subsumes every consumer's affinity gate).
//
// Specific masks are between bottom and top, ordered by inclusion.
//
// ── Why this is a DIFFERENT type from BitsBudget / PeakBytes ───────
//
// AffinityMask wraps uint64_t but the SEMANTICS are bit-set
// (boolean lattice), not numeric counter (chain lattice).  The
// strong-newtype identity ensures cross-axis mixing — e.g.,
// passing an AffinityMask where a BitsBudget is expected — is a
// compile error.  Same discipline as the other strong newtypes;
// see Budgeted.h cross-axis-disjointness assertion.
//
//   Axiom coverage:
//     TypeSafe — AffinityMask is a strong-tagged uint64_t newtype.
//     DetSafe — leq / join / meet are all `constexpr`.
//   Runtime cost:
//     element_type = AffinityMask = uint64_t + 0 phantom bytes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── AffinityMask — strong-typed uint64_t bitmask of CPU cores ─────
//
// Bit i represents core i (i ∈ 0..63).  Distinct C++ struct from
// BitsBudget / PeakBytes / Epoch / Generation even though all are
// uint64_t-backed; phantom typing prevents axis-swap bugs.
struct AffinityMask {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(AffinityMask const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(AffinityMask const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }

    // ── Set-style helpers ─────────────────────────────────────────
    //
    // Convenience operations for AffinityMask — set the bit for a
    // specific core, test whether a core is admitted, etc.
    // Designed for production call sites that build affinities
    // incrementally.

    // Construct an AffinityMask containing exactly one core.
    [[nodiscard]] static constexpr AffinityMask single(std::uint8_t core) noexcept {
        return AffinityMask{std::uint64_t{1} << core};
    }

    // Test whether core is admitted by this affinity.
    [[nodiscard]] constexpr bool contains(std::uint8_t core) const noexcept {
        return (value & (std::uint64_t{1} << core)) != 0;
    }

    // Population count — number of cores admitted.
    [[nodiscard]] constexpr std::uint8_t popcount() const noexcept {
        return static_cast<std::uint8_t>(__builtin_popcountll(value));
    }
};

// ── AffinityLattice — boolean lattice over AffinityMask ────────────
struct AffinityLattice {
    using element_type = AffinityMask;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{0};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        // Set inclusion: a ⊆ b iff (a & b) == a.
        return (a.value & b.value) == a.value;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value | b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value & b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "AffinityLattice";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::affinity_lattice_self_test {

static_assert(Lattice<AffinityLattice>);
static_assert(BoundedLattice<AffinityLattice>);
static_assert(!UnboundedLattice<AffinityLattice>);
static_assert(!Semiring<AffinityLattice>);

static_assert(sizeof(AffinityMask) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<AffinityMask>);
static_assert(std::is_standard_layout_v<AffinityMask>);

static_assert(!std::is_same_v<AffinityMask, std::uint64_t>);

// ── Set-helper witnesses ─────────────────────────────────────────
static_assert(AffinityMask::single(0).value == 0b1);
static_assert(AffinityMask::single(3).value == 0b1000);
static_assert(AffinityMask::single(0).contains(0));
static_assert(!AffinityMask::single(0).contains(1));
static_assert(AffinityMask{0b1010}.contains(1));
static_assert(AffinityMask{0b1010}.contains(3));
static_assert(!AffinityMask{0b1010}.contains(0));
static_assert(AffinityMask{0b1010}.popcount() == 2);
static_assert(AffinityMask{0}.popcount()      == 0);

// ── Ordering witnesses (set inclusion) ───────────────────────────
static_assert( AffinityLattice::leq(AffinityMask{0},     AffinityMask{0b111}));
static_assert( AffinityLattice::leq(AffinityMask{0b001}, AffinityMask{0b111}));
static_assert( AffinityLattice::leq(AffinityMask{0b010}, AffinityMask{0b111}));
static_assert( AffinityLattice::leq(AffinityMask{0b111}, AffinityMask{0b111}));   // reflexive
static_assert(!AffinityLattice::leq(AffinityMask{0b111}, AffinityMask{0b001}));
static_assert(!AffinityLattice::leq(AffinityMask{0b001}, AffinityMask{0b010}));   // disjoint
static_assert(!AffinityLattice::leq(AffinityMask{0b011}, AffinityMask{0b101}));   // overlapping
                                                                                   // but neither
                                                                                   // ⊆ the other

// ── Bounds ───────────────────────────────────────────────────────
static_assert(AffinityLattice::bottom().value == 0);
static_assert(AffinityLattice::top().value    == std::numeric_limits<std::uint64_t>::max());

// ── Join / meet (bitwise) ────────────────────────────────────────
static_assert(AffinityLattice::join(AffinityMask{0b001}, AffinityMask{0b010}).value == 0b011);
static_assert(AffinityLattice::meet(AffinityMask{0b011}, AffinityMask{0b110}).value == 0b010);

// Bound identities.
static_assert(AffinityLattice::join(AffinityMask{0b101}, AffinityLattice::bottom())
              == AffinityMask{0b101});
static_assert(AffinityLattice::meet(AffinityMask{0b101}, AffinityLattice::top())
              == AffinityMask{0b101});

// Idempotence.
static_assert(AffinityLattice::join(AffinityMask{0b111}, AffinityMask{0b111})
              == AffinityMask{0b111});
static_assert(AffinityLattice::meet(AffinityMask{0b111}, AffinityMask{0b111})
              == AffinityMask{0b111});

// ── Distributivity at three witnesses ─────────────────────────────
[[nodiscard]] consteval bool distributive_witness() noexcept {
    AffinityMask a{0b1100};
    AffinityMask b{0b0110};
    AffinityMask c{0b1010};
    auto         lhs = AffinityLattice::meet(a, AffinityLattice::join(b, c));
    auto         rhs = AffinityLattice::join(AffinityLattice::meet(a, b),
                                             AffinityLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Verify Boolean-lattice complement law (sanity, not a Lattice
// concept requirement — boolean lattices have a complement
// operation that other lattices don't).
[[nodiscard]] consteval bool complement_witness() noexcept {
    AffinityMask a   {0b00001111};
    AffinityMask cmp {~a.value};
    return AffinityLattice::join(a, cmp) == AffinityLattice::top()
        && AffinityLattice::meet(a, cmp) == AffinityLattice::bottom();
}
static_assert(complement_witness());

// Implicit conversion DOWN to uint64_t.
static_assert([] consteval {
    AffinityMask  m{0b1010};
    std::uint64_t n = m;
    return n == 0b1010;
}());

inline void runtime_smoke_test() {
    AffinityMask                  bot   = AffinityLattice::bottom();
    AffinityMask                  topv  = AffinityLattice::top();
    AffinityMask                  cores_2_to_5{0b00111100};
    [[maybe_unused]] bool         l     = AffinityLattice::leq(bot, topv);
    [[maybe_unused]] AffinityMask j     = AffinityLattice::join(cores_2_to_5, topv);
    [[maybe_unused]] AffinityMask m     = AffinityLattice::meet(cores_2_to_5, bot);

    // Set helpers.
    AffinityMask                  core_0  = AffinityMask::single(0);
    AffinityMask                  core_42 = AffinityMask::single(42);
    if (!core_0.contains(0))   std::abort();
    if ( core_0.contains(1))   std::abort();
    if (!core_42.contains(42)) std::abort();
    if (core_42.popcount() != 1u) std::abort();

    // Composing affinities: union (join).
    AffinityMask                  joined = AffinityLattice::join(core_0, core_42);
    if (!joined.contains(0))  std::abort();
    if (!joined.contains(42)) std::abort();

    // Intersection (meet).
    AffinityMask                  intersected =
        AffinityLattice::meet(AffinityMask{0b1100}, AffinityMask{0b0110});
    if (intersected.value != 0b0100) std::abort();

    std::uint64_t                 total = cores_2_to_5;
    if (total != 0b00111100u) std::abort();

    // Lattice over Graded substrate.
    using AffinityGraded = Graded<ModalityKind::Absolute, AffinityLattice, double>;
    AffinityGraded                v{3.14, AffinityMask{0b00111100}};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::affinity_lattice_self_test

}  // namespace crucible::algebra::lattices
