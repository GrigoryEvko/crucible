// ═══════════════════════════════════════════════════════════════════
// prop_vector_clock.cpp — partial-order + round-trip fuzzer for the
// canopy VectorClock snapshot (canopy/VectorClock.h, backed by
// algebra::lattices::HappensBeforeLattice).
//
// VectorClockSnapshot is the comparison surface canopy uses to recover
// causality across the fleet: happens_before / concurrent_with /
// comparable_with / operator<=> drive conflict detection, anti-entropy,
// and reshard ordering.  Get the partial order wrong — e.g. treat an
// absent node as anything but 0, or use <= where < is required for the
// strict happens-before — and the mesh either misses a real conflict or
// invents a phantom one.
//
// test_vector_clock.cpp pins hand-picked assertions; no property fuzzer
// existed.  The INDEPENDENT oracle here re-derives the entire partial
// order from the DENSE definition (∀i a[i] <= b[i]) — a different
// computation from the lattice's logic — so a divergence is a genuine
// bug.  The generator biases toward "close" clocks (shared base + per-
// side increments) so dominance, equality, AND concurrency all occur
// densely, plus an independent-corner mode; entries hit {0, 1, small,
// MAX}.  Per (a, b) over 8 nodes it asserts:
//
//   * operator<=>  matches {equivalent | less | greater | unordered}
//     derived from dense le(a,b) / le(b,a)
//   * happens_before(a,b)  ⟺  le(a,b) ∧ a != b
//   * concurrent_with(a,b) ⟺  ¬le(a,b) ∧ ¬le(b,a)
//   * comparable_with(a,b) ⟺  le(a,b) ∨ le(b,a)
//   * concurrency is symmetric; happens_before is anti-symmetric
//   * reflexivity: a<=>a equivalent, ¬happens_before(a,a),
//     ¬concurrent_with(a,a), comparable_with(a,a)
//   * round-trips: from_sparse_delta(a.sparse_delta()) == a and
//     from_lattice_clock(a.as_lattice_clock()) == a
//
// All verified clean by hand-trace; the order is correct — this is the
// full-domain dense-oracle regression net the spot test lacked.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/VectorClock.h>

#include <array>
#include <compare>
#include <cstdint>
#include <limits>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::size_t kNodes = 8;
using Snapshot = cc::VectorClockSnapshot<kNodes>;
inline constexpr std::uint64_t kVMax = std::numeric_limits<std::uint64_t>::max();

struct Spec {
    std::array<std::uint64_t, kNodes> a{};
    std::array<std::uint64_t, kNodes> b{};
};

[[nodiscard]] std::uint64_t gen_count(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return kVMax;
        case 3: return rng.next_below(8u);   // small cluster → frequent ties
        case 4: return kVMax - 1u;
        default: return rng.next64();
    }
}

// Dense partial order: a <= b iff every component is <=.
[[nodiscard]] bool dense_le(const std::array<std::uint64_t, kNodes>& a,
                            const std::array<std::uint64_t, kNodes>& b) noexcept {
    for (std::size_t i = 0; i < kNodes; ++i) {
        if (a[i] > b[i]) return false;
    }
    return true;
}

[[nodiscard]] Snapshot make_snapshot(
    const std::array<std::uint64_t, kNodes>& counts) noexcept {
    Snapshot snap{};
    for (std::size_t i = 0; i < kNodes; ++i) {
        snap.entries[i] = counts[i];
    }
    return snap;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("vector_clock", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            if (rng.next_below(2u) == 0u) {
                // Independent corner-biased clocks.
                for (std::size_t i = 0; i < kNodes; ++i) {
                    spec.a[i] = gen_count(rng);
                    spec.b[i] = gen_count(rng);
                }
            } else {
                // Shared base + per-side increments — makes dominance,
                // equality, and concurrency all common.
                for (std::size_t i = 0; i < kNodes; ++i) {
                    const std::uint64_t base = rng.next_below(8u);
                    const std::uint64_t da = rng.next_below(4u);
                    const std::uint64_t db = rng.next_below(4u);
                    spec.a[i] = base + da;
                    spec.b[i] = base + db;
                }
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            const Snapshot a = make_snapshot(spec.a);
            const Snapshot b = make_snapshot(spec.b);

            const bool le_ab = dense_le(spec.a, spec.b);
            const bool le_ba = dense_le(spec.b, spec.a);
            const bool eq = (le_ab && le_ba);  // dense equality

            // ── operator<=> vs dense oracle ──
            const std::partial_ordering got = (a <=> b);
            const std::partial_ordering want =
                eq      ? std::partial_ordering::equivalent
                : le_ab ? std::partial_ordering::less
                : le_ba ? std::partial_ordering::greater
                        : std::partial_ordering::unordered;
            if (got != want) return false;
            if ((a == b) != eq) return false;

            // ── happens_before / concurrent / comparable ──
            if (a.happens_before(b) != (le_ab && !eq)) return false;
            if (b.happens_before(a) != (le_ba && !eq)) return false;
            if (a.concurrent_with(b) != (!le_ab && !le_ba)) return false;
            if (a.comparable_with(b) != (le_ab || le_ba)) return false;

            // ── symmetry / anti-symmetry ──
            if (a.concurrent_with(b) != b.concurrent_with(a)) return false;
            if (a.happens_before(b) && b.happens_before(a)) return false;

            // ── reflexivity ──
            if ((a <=> a) != std::partial_ordering::equivalent) return false;
            if (a.happens_before(a)) return false;
            if (a.concurrent_with(a)) return false;
            if (!a.comparable_with(a)) return false;

            // ── round-trips ──
            if (!(Snapshot::from_sparse_delta(a.sparse_delta()) == a)) return false;
            if (!(Snapshot::from_lattice_clock(a.as_lattice_clock()) == a)) return false;

            return true;
        });
}
