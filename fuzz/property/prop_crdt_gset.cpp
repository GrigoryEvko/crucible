// ═══════════════════════════════════════════════════════════════════
// prop_crdt_gset.cpp — semilattice-law fuzzer for the canopy grow-only
// set CRDT (canopy/Crdt.h GSet / BoundedHashSetState).
//
// GSet is a state-based CRDT: replicas converge by repeatedly merging
// each other's full state, and convergence is GUARANTEED only if merge
// is a join-semilattice operation — commutative, associative, and
// idempotent — whose result is the least upper bound (set union).  If
// any law breaks, two replicas that received the same updates in a
// different order can disagree forever (split-brain in canopy's
// eventually-consistent gossip state).  These laws are the entire
// correctness contract of a state CRDT, yet test_crdt.cpp pins only
// hand-picked cases and no property fuzzer existed.
//
// The independent oracle is a 32-bit BITSET UNION — a completely
// different computation from the open-addressed hash-table merge under
// test — so a divergence is a genuine bug.  Universe is the 32 values
// [0,32); Capacity is GSet's default 64 > 32, so merge never overflows
// (the only path on which it would return its left operand instead of
// the union).  Three subsets a,b,c are drawn as corner-biased 32-bit
// masks.  Per (a,b,c) it asserts:
//
//   * build faithfulness: state(mask).contains(i) ⟺ bit i of mask,
//     and size() == popcount(mask)
//   * union/LUB oracle: merge(a,b).contains(i) ⟺ a_i ∨ b_i  (every i)
//   * commutativity:  merge(a,b) == merge(b,a)
//   * idempotence:    merge(a,a) == a
//   * associativity:  merge(merge(a,b),c) == merge(a,merge(b,c))
//   * dominance:      a_i ⇒ merge(a,b).contains(i)  (merge never drops)
//
// state_type's operator== is membership equality (count + contains), so
// it correctly ignores internal hash-slot arrangement — two unions that
// differ only in probe order still compare equal.  The laws hold across
// the domain; this is the convergence-safety net the unit test lacked.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <bit>
#include <cstdint>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

using GS = cc::GSet<std::uint32_t>;       // Capacity defaults to 64
using State = GS::state_type;
inline constexpr std::uint32_t kUniverse = 32;  // < 64, so merge never overflows

struct Spec {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

[[nodiscard]] std::uint32_t gen_mask(Rng& rng) noexcept {
    switch (rng.next_below(5u)) {
        case 0: return 0u;
        case 1: return 0xFFFF'FFFFu;
        case 2: return std::uint32_t{1} << rng.next_below(kUniverse);  // single element
        case 3: return rng.next32() & rng.next32();                    // sparse
        default: return rng.next32();
    }
}

[[nodiscard]] State from_mask(std::uint32_t mask) noexcept {
    State s{};
    for (std::uint32_t i = 0; i < kUniverse; ++i) {
        if ((mask >> i) & 1u) {
            (void)s.insert(i);  // Capacity 64 > universe 32 → always succeeds
        }
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("crdt_gset", cfg,
        [](Rng& rng) noexcept -> Spec {
            return Spec{gen_mask(rng), gen_mask(rng), gen_mask(rng)};
        },
        [](const Spec& spec) noexcept -> bool {
            const State a = from_mask(spec.a);
            const State b = from_mask(spec.b);
            const State c = from_mask(spec.c);

            // Build faithfulness + size == popcount (spec.a is exactly the
            // 32-bit universe, so popcount(spec.a) is the element count).
            if (a.size().value() != std::popcount(spec.a)) return false;
            for (std::uint32_t i = 0; i < kUniverse; ++i) {
                if (a.contains(i) != (((spec.a >> i) & 1u) != 0u)) return false;
            }

            const State ab = GS::merge(a, b);
            const State ba = GS::merge(b, a);

            // Union / least-upper-bound oracle (independent bitset OR).
            const std::uint32_t uni = spec.a | spec.b;
            for (std::uint32_t i = 0; i < kUniverse; ++i) {
                const bool want = ((uni >> i) & 1u) != 0u;
                if (ab.contains(i) != want) return false;
                // Dominance: every element of a survives the merge.
                if (((spec.a >> i) & 1u) && !ab.contains(i)) return false;
                if (((spec.b >> i) & 1u) && !ab.contains(i)) return false;
            }

            // Commutativity.
            if (!(ab == ba)) return false;
            // Idempotence.
            if (!(GS::merge(a, a) == a)) return false;
            // Associativity.
            const State left = GS::merge(GS::merge(a, b), c);
            const State right = GS::merge(a, GS::merge(b, c));
            if (!(left == right)) return false;

            return true;
        });
}
