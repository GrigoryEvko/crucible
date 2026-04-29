#pragma once

// ── crucible::algebra::lattices::AffinityLattice ────────────────────
//
// BOOLEAN-ALGEBRA lattice over a CPU affinity bitmask sized for
// modern production hardware.  A powerset lattice over the universe
// of CPU core IDs (0..kMaxCore inclusive).
//
// THE CORE-COUNT BUDGET:
//
//   kWords    = 4               (compile-time constant)
//   kBits     = kWords × 64     = 256
//   kMaxCore  = kBits - 1       = 255
//
// 256 cores per affinity mask covers every shipped server CPU as
// of 2026: AMD Bergamo (192 cores / 384 SMT-1), AMD Genoa (96
// cores / 192 SMT-2), ARM AmpereOne (192 cores), Apple Silicon
// (12-24 cores), NVIDIA Grace (72 cores).  Intel Sierra Forest
// (288 cores) crosses the boundary — fleets with that hardware
// must bump kWords to 5 (320 cores) at compile time.  Centralizing
// the constant makes the bump a single-line change.
//
// Multi-word backing (NOT std::bitset): an aggregate of four
// uint64_t.  Layout-stable (32 bytes flat), trivially-copyable,
// constexpr-friendly, and amenable to SIMD widening if a future
// hot path needs sub-cycle bitmap operations.  std::bitset would
// be functionally equivalent but its libstdc++ implementation is
// not always-trivial-relocatable, so the inline aggregate is the
// safer choice for layout-critical Graded substrates.
//
// Sister axis to NumaNodeLattice; the second component sub-lattice
// for the NumaPlacement product wrapper from 28_04_2026_effects.md
// §4.4.3 (FOUND-G71).
//
// Citation: THREADING.md §5.4 (cache-tier rule with NUMA-local
// placement); CRUCIBLE.md §L13 (NUMA-aware ThreadPool worker
// affinity).
//
// ── Algebraic shape (boolean lattice) ───────────────────────────────
//
// Carrier:  AffinityMask = strong-typed array<uint64_t, kWords>.
// Order:    leq(a, b) = a ⊆ b  (set inclusion, componentwise)
// Bottom:   AffinityMask{}                   (all-zero — admits no core)
// Top:      AffinityMask{~0, ~0, ..., ~0}    (all kBits cores)
// Join:     componentwise OR
// Meet:     componentwise AND
//
// Distributive boolean lattice; complements via componentwise NOT.
//
// ── Direction convention ────────────────────────────────────────────
//
// Bigger set = HIGHER in the lattice = more permissive.  Same as
// the four sister uint64-backed lattices.  Top = wildcard,
// bottom = pinned-to-nothing.
//
//   Axiom coverage:
//     TypeSafe — AffinityMask is a strong-tagged aggregate; mixing
//                with the four sister uint64-backed newtypes is a
//                compile error.
//     DetSafe — leq / join / meet are all `constexpr`.
//     MemSafe — trivially copyable; no heap.
//   Runtime cost:
//     element_type = AffinityMask = kWords * 8 = 32 bytes.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── AffinityMask — strong-typed multi-word CPU bitmask ─────────────
//
// 256-bit mask via four uint64_t words.  Bit (word*64 + lane)
// represents core (word*64 + lane).  Distinct C++ struct from
// every sister strong newtype (BitsBudget / PeakBytes / Epoch /
// Generation) — phantom-tagged by structure name only; no implicit
// conversion to or from any other axis.
struct AffinityMask {
    static constexpr std::size_t  kWords    = 4;
    static constexpr std::size_t  kBits     = kWords * 64;     // 256
    static constexpr std::uint16_t kMaxCore = static_cast<std::uint16_t>(kBits - 1);

    std::array<std::uint64_t, kWords> words{};

    [[nodiscard]] constexpr bool operator==(AffinityMask const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(AffinityMask const&) const noexcept = default;

    // ── Set-style helpers ─────────────────────────────────────────

    // Construct an AffinityMask containing exactly one core.
    //
    // CONTRACT: core ≤ kMaxCore.  Without this fence a caller passing
    // core ≥ kBits would silently produce zero (or, on platforms
    // where the array bounds are checked at runtime, abort).
    [[nodiscard]] static constexpr AffinityMask single(std::uint16_t core) noexcept
        pre (core <= kMaxCore)
    {
        AffinityMask m{};
        m.words[core / 64] = std::uint64_t{1} << (core % 64);
        return m;
    }

    // Test whether core is admitted by this affinity.
    //
    // CONTRACT: core ≤ kMaxCore.  Same UB rationale as `single`.
    [[nodiscard]] constexpr bool contains(std::uint16_t core) const noexcept
        pre (core <= kMaxCore)
    {
        return (words[core / 64] & (std::uint64_t{1} << (core % 64))) != 0;
    }

    // Population count — number of cores admitted across all words.
    [[nodiscard]] constexpr std::uint16_t popcount() const noexcept {
        std::uint16_t count = 0;
        for (auto w : words) {
            count = static_cast<std::uint16_t>(count + __builtin_popcountll(w));
        }
        return count;
    }

    // Construct a mask with all cores in the contiguous range
    // [first_core, last_core] (inclusive) admitted.  Convenience
    // factory for production NumaThreadPool worker assignment over
    // a per-NUMA-node core range.
    //
    // CONTRACT: first_core ≤ last_core ≤ kMaxCore.
    [[nodiscard]] static constexpr AffinityMask range(std::uint16_t first_core,
                                                       std::uint16_t last_core) noexcept
        pre (first_core <= last_core)
        pre (last_core  <= kMaxCore)
    {
        AffinityMask m{};
        for (std::uint16_t c = first_core; c <= last_core; ++c) {
            m.words[c / 64] |= std::uint64_t{1} << (c % 64);
        }
        return m;
    }
};

// ── AffinityLattice — boolean lattice over AffinityMask ────────────
struct AffinityLattice {
    using element_type = AffinityMask;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{};
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type m{};
        for (auto& w : m.words) w = std::numeric_limits<std::uint64_t>::max();
        return m;
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        // Set inclusion: a ⊆ b iff (a & b) == a, componentwise.
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            if ((a.words[i] & b.words[i]) != a.words[i]) return false;
        }
        return true;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] | b.words[i];
        }
        return r;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] & b.words[i];
        }
        return r;
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

// Layout assertions.
static_assert(AffinityMask::kWords    == 4);
static_assert(AffinityMask::kBits     == 256);
static_assert(AffinityMask::kMaxCore  == 255);
static_assert(sizeof(AffinityMask) == AffinityMask::kWords * sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<AffinityMask>);
static_assert(std::is_standard_layout_v<AffinityMask>);

// AffinityMask is structurally distinct from any plain integer type
// — the cross-axis disjointness fence relies on this.
static_assert(!std::is_same_v<AffinityMask, std::uint64_t>);
static_assert(!std::is_same_v<AffinityMask, std::array<std::uint64_t, 4>>);

// ── Set-helper witnesses ─────────────────────────────────────────
static_assert(AffinityMask::single(0).words[0]   == 0b1);
static_assert(AffinityMask::single(3).words[0]   == 0b1000);
static_assert(AffinityMask::single(63).words[0]  == (std::uint64_t{1} << 63));
static_assert(AffinityMask::single(64).words[1]  == std::uint64_t{1});  // crosses word
static_assert(AffinityMask::single(127).words[1] == (std::uint64_t{1} << 63));
static_assert(AffinityMask::single(128).words[2] == std::uint64_t{1});
static_assert(AffinityMask::single(192).words[3] == std::uint64_t{1});
static_assert(AffinityMask::single(255).words[3] == (std::uint64_t{1} << 63));

static_assert(AffinityMask::single(0).contains(0));
static_assert(!AffinityMask::single(0).contains(1));
static_assert(AffinityMask::single(127).contains(127));
static_assert(!AffinityMask::single(127).contains(128));
static_assert(AffinityMask::single(192).contains(192));   // Bergamo upper boundary

// Boundary: highest valid core (kMaxCore = 255) must succeed.
static_assert(AffinityMask::single(AffinityMask::kMaxCore).contains(255));
static_assert(!AffinityMask::single(AffinityMask::kMaxCore).contains(0));

// Range factory.
static_assert(AffinityMask::range(0, 3).contains(0));
static_assert(AffinityMask::range(0, 3).contains(3));
static_assert(!AffinityMask::range(0, 3).contains(4));
static_assert(AffinityMask::range(60, 70).contains(60));
static_assert(AffinityMask::range(60, 70).contains(63));   // last bit of word 0
static_assert(AffinityMask::range(60, 70).contains(64));   // first bit of word 1
static_assert(AffinityMask::range(60, 70).contains(70));
static_assert(!AffinityMask::range(60, 70).contains(71));
static_assert(AffinityMask::range(0, 191).popcount() == 192);  // full Bergamo
static_assert(AffinityMask::range(0, 255).popcount() == 256);  // full mask

// popcount.
static_assert(AffinityMask::single(0).popcount()   == 1);
static_assert(AffinityMask::single(127).popcount() == 1);
static_assert(AffinityMask::single(255).popcount() == 1);
static_assert(AffinityLattice::bottom().popcount() == 0);
static_assert(AffinityLattice::top().popcount()    == AffinityMask::kBits);

// ── Ordering witnesses (set inclusion, componentwise) ─────────────
static_assert( AffinityLattice::leq(AffinityMask{},
                                    AffinityMask::range(0, 7)));
static_assert( AffinityLattice::leq(AffinityMask::single(127),
                                    AffinityMask::range(127, 192)));
static_assert(!AffinityLattice::leq(AffinityMask::single(127),
                                    AffinityMask::range(0, 63)));     // wrong word
static_assert( AffinityLattice::leq(AffinityLattice::bottom(),
                                    AffinityLattice::top()));

// ── Bounds ───────────────────────────────────────────────────────
static_assert(AffinityLattice::bottom() == AffinityMask{});

// Top has every bit set.
[[nodiscard]] consteval bool top_has_all_bits() noexcept {
    auto t = AffinityLattice::top();
    for (auto w : t.words) {
        if (w != std::numeric_limits<std::uint64_t>::max()) return false;
    }
    return true;
}
static_assert(top_has_all_bits());

// ── Join / meet (componentwise bitwise) ──────────────────────────
[[nodiscard]] consteval bool join_meet_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 63);            // word 0 full
    AffinityMask b = AffinityMask::range(64, 127);          // word 1 full
    AffinityMask jab = AffinityLattice::join(a, b);
    AffinityMask mab = AffinityLattice::meet(a, b);
    return  jab.popcount() == 128                           // disjoint union
        &&  mab.popcount() == 0                             // disjoint intersection
        &&  jab.contains(0)
        &&  jab.contains(127)
        && !mab.contains(0);
}
static_assert(join_meet_witness());

// Bound identities.
[[nodiscard]] consteval bool bound_identities() noexcept {
    AffinityMask m = AffinityMask::single(192);
    return  AffinityLattice::join(m, AffinityLattice::bottom()) == m
        &&  AffinityLattice::meet(m, AffinityLattice::top())    == m;
}
static_assert(bound_identities());

// Idempotence.
[[nodiscard]] consteval bool idempotence_witness() noexcept {
    AffinityMask m = AffinityMask::range(0, 191);
    return  AffinityLattice::join(m, m) == m
        &&  AffinityLattice::meet(m, m) == m;
}
static_assert(idempotence_witness());

// ── Distributivity at three witnesses (boolean lattice) ───────────
[[nodiscard]] consteval bool distributive_witness() noexcept {
    AffinityMask a = AffinityMask::range(0,   63);
    AffinityMask b = AffinityMask::range(32,  95);
    AffinityMask c = AffinityMask::range(64, 127);
    auto         lhs = AffinityLattice::meet(a, AffinityLattice::join(b, c));
    auto         rhs = AffinityLattice::join(AffinityLattice::meet(a, b),
                                             AffinityLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

// Boolean-lattice complement law.
[[nodiscard]] consteval bool complement_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 31);
    AffinityMask cmp{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        cmp.words[i] = ~a.words[i];
    }
    return AffinityLattice::join(a, cmp) == AffinityLattice::top()
        && AffinityLattice::meet(a, cmp) == AffinityLattice::bottom();
}
static_assert(complement_witness());

// ── Transitivity of set inclusion ─────────────────────────────────
[[nodiscard]] consteval bool transitivity_witness() noexcept {
    AffinityMask small  = AffinityMask::single(7);
    AffinityMask medium = AffinityMask::range(0,  31);
    AffinityMask large  = AffinityMask::range(0, 191);    // Bergamo-shape
    return  AffinityLattice::leq(small,  medium)
        &&  AffinityLattice::leq(medium, large)
        &&  AffinityLattice::leq(small,  large)
        &&  AffinityLattice::leq(AffinityLattice::bottom(), small)
        &&  AffinityLattice::leq(large, AffinityLattice::top());
}
static_assert(transitivity_witness());

// ── De Morgan's laws ──────────────────────────────────────────────
[[nodiscard]] consteval bool de_morgan_witness() noexcept {
    AffinityMask a = AffinityMask::range(0,   31);
    AffinityMask b = AffinityMask::range(16,  47);
    AffinityMask cmp_a{}, cmp_b{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        cmp_a.words[i] = ~a.words[i];
        cmp_b.words[i] = ~b.words[i];
    }

    // ¬(a ∨ b) = ¬a ∧ ¬b
    AffinityMask not_join_ab{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        not_join_ab.words[i] = ~AffinityLattice::join(a, b).words[i];
    }
    AffinityMask meet_neg = AffinityLattice::meet(cmp_a, cmp_b);
    bool law1 = not_join_ab == meet_neg;

    // ¬(a ∧ b) = ¬a ∨ ¬b
    AffinityMask not_meet_ab{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        not_meet_ab.words[i] = ~AffinityLattice::meet(a, b).words[i];
    }
    AffinityMask join_neg = AffinityLattice::join(cmp_a, cmp_b);
    bool law2 = not_meet_ab == join_neg;

    return law1 && law2;
}
static_assert(de_morgan_witness());

inline void runtime_smoke_test() {
    AffinityMask                  bot   = AffinityLattice::bottom();
    AffinityMask                  topv  = AffinityLattice::top();
    AffinityMask                  bergamo_full = AffinityMask::range(0, 191);

    [[maybe_unused]] bool         l1    = AffinityLattice::leq(bot,           topv);
    [[maybe_unused]] bool         l2    = AffinityLattice::leq(bergamo_full,  topv);
    [[maybe_unused]] AffinityMask j     = AffinityLattice::join(bergamo_full, bot);
    [[maybe_unused]] AffinityMask m     = AffinityLattice::meet(bergamo_full, topv);

    if (bergamo_full.popcount() != 192) std::abort();

    AffinityMask                  core_0       = AffinityMask::single(0);
    AffinityMask                  core_191     = AffinityMask::single(191);   // Bergamo top
    AffinityMask                  core_255     = AffinityMask::single(255);   // mask top
    if (!core_0.contains(0))     std::abort();
    if (!core_191.contains(191)) std::abort();
    if (!core_255.contains(255)) std::abort();
    if ( core_0.contains(1))     std::abort();

    AffinityMask                  joined = AffinityLattice::join(core_0, core_191);
    if (!joined.contains(0))   std::abort();
    if (!joined.contains(191)) std::abort();

    AffinityMask                  intersected =
        AffinityLattice::meet(AffinityMask::range(0, 127), AffinityMask::range(64, 191));
    if (intersected.popcount() != 64) std::abort();    // word 1 only

    using AffinityGraded = Graded<ModalityKind::Absolute, AffinityLattice, double>;
    AffinityGraded                v{3.14, AffinityMask::range(0, 31)};
    [[maybe_unused]] auto         g  = v.grade();
    [[maybe_unused]] auto         vp = v.peek();
}

}  // namespace detail::affinity_lattice_self_test

}  // namespace crucible::algebra::lattices
