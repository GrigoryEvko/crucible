// ═══════════════════════════════════════════════════════════════════
// prop_crdt_mvregister.cpp — antichain-merge fuzzer for the canopy
// multi-value register CRDT (canopy/Crdt.h MVRegister).
//
// MVRegister is the last and hardest of canopy's state-based CRDTs:
// GSet/OrSet (sets) and LwwRegister/GCounter/PNCounter (total-order /
// per-replica) are already fuzzed; MVRegister keeps the set of
// CONCURRENT versions — its merge retains exactly the maximal elements
// of the two version sets under the vector-clock happens-before partial
// order (a write strictly causally dominated by another is pruned; two
// causally-concurrent writes both survive, giving the "multi-value"
// conflict set).  Canopy replicas converge only if this merge is a
// join-semilattice op (commutative / associative / idempotent / LUB);
// one mis-pruned version or a dropped concurrent write splits the
// fleet's register state permanently.  test_crdt.cpp pins hand-picked
// cases; no property fuzzer existed.
//
// States are built the way production builds them — by assign()-ing K
// random (value, vector-clock) versions into a fresh MVRegister, which
// canonicalises to the maximal antichain — so a, b, c are genuine
// antichains exactly as the runtime produces.  The INDEPENDENT oracle
// re-derives the merge as a set-level Pareto frontier: union the two
// version sets, dedup by exact (value, clock) identity, then keep every
// version NOT strictly dominated by another, where happens-before is
// recomputed from the dense clock `entries` as (∀i p≤q) ∧ (∃i p<q) — a
// completely different formulation from production's incremental
// insert-and-prune (clock_less_ / happens_before / canonicalize_).  A
// divergence is therefore a real bug.  Per (a,b,c) it asserts:
//
//   * merge oracle: MVRegister::merge(a,b)'s version SET equals the
//     maximal antichain of a∪b (order-independent set equality, since
//     canonicalize_ sorts internally but only the set is semantic)
//   * antichain invariant: no version in the result is happens-before
//     another (the result is a valid antichain)
//   * containment: every result version came from a or b (no synthesis)
//   * commutativity:  merge(a,b) ≡ merge(b,a)
//   * idempotence:    merge(a,a) ≡ a
//   * associativity:  merge(merge(a,b),c) ≡ merge(a,merge(b,c))
//
// MaxNodes=4, MaxVersions=16, V=u32, clock entries and values drawn from
// [0,3) so causal domination and concurrency both fire densely while the
// union stays well under MaxVersions (≤4 versions/state × 3 states = 12
// < 16 → merge never overflows).  This is the convergence-safety net the
// unit test lacked, now for the multi-value register too.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::size_t kNodes = 4;
inline constexpr std::size_t kMaxVersions = 16;

using MV = cc::MVRegister<std::uint32_t, kMaxVersions, kNodes>;
using State = MV::state_type;
using Version = MV::version_type;
using LocalV = MV::local_write_type;

// ─── independent happens-before / equality (from dense entries) ────

// true iff y.clock strictly happens-before x.clock: (∀i y≤x) ∧ (∃i y<x).
// Re-derived from entries, NOT production's clock_less_/happens_before.
[[nodiscard]] bool clock_dominates(const Version& y, const Version& x) noexcept {
    bool all_le = true;
    bool any_lt = false;
    for (std::size_t i = 0; i < kNodes; ++i) {
        const std::uint64_t yi = y.clock.entries[i];
        const std::uint64_t xi = x.clock.entries[i];
        if (yi > xi) all_le = false;
        if (yi < xi) any_lt = true;
    }
    return all_le && any_lt;
}

[[nodiscard]] bool version_eq(const Version& a, const Version& b) noexcept {
    if (a.value != b.value) return false;
    for (std::size_t i = 0; i < kNodes; ++i) {
        if (a.clock.entries[i] != b.clock.entries[i]) return false;
    }
    return true;
}

// ─── oracle: maximal antichain of two states' version union ────────

struct VerSet {
    std::array<Version, 2 * kMaxVersions> v{};
    std::size_t n = 0;
};

[[nodiscard]] VerSet oracle_merge(const State& a, const State& b) noexcept {
    VerSet uni{};
    const auto add_unique = [&](const Version& x) {
        for (std::size_t i = 0; i < uni.n; ++i) {
            if (version_eq(uni.v[i], x)) return;
        }
        uni.v[uni.n] = x;
        ++uni.n;
    };
    for (std::uint16_t i = 0; i < a.count; ++i) add_unique(a.versions[i]);
    for (std::uint16_t i = 0; i < b.count; ++i) add_unique(b.versions[i]);

    VerSet kept{};
    for (std::size_t i = 0; i < uni.n; ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < uni.n; ++j) {
            if (i != j && clock_dominates(uni.v[i], uni.v[j])) {
                dominated = true;
                break;
            }
        }
        if (!dominated) {
            kept.v[kept.n] = uni.v[i];
            ++kept.n;
        }
    }
    return kept;
}

// ─── set-equality helpers (both sides are deduped antichains, so
//     count match + one-way subset ⟹ set equality) ─────────────────

[[nodiscard]] bool state_eq_oracle(const State& s, const VerSet& k) noexcept {
    if (s.count != k.n) return false;
    for (std::uint16_t i = 0; i < s.count; ++i) {
        bool found = false;
        for (std::size_t j = 0; j < k.n; ++j) {
            if (version_eq(s.versions[i], k.v[j])) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

[[nodiscard]] bool state_eq(const State& x, const State& y) noexcept {
    if (x.count != y.count) return false;
    for (std::uint16_t i = 0; i < x.count; ++i) {
        bool found = false;
        for (std::uint16_t j = 0; j < y.count; ++j) {
            if (version_eq(x.versions[i], y.versions[j])) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

[[nodiscard]] bool is_antichain(const State& s) noexcept {
    for (std::uint16_t i = 0; i < s.count; ++i) {
        for (std::uint16_t j = 0; j < s.count; ++j) {
            if (i != j && clock_dominates(s.versions[i], s.versions[j])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool in_either(const Version& w, const State& a, const State& b) noexcept {
    for (std::uint16_t i = 0; i < a.count; ++i) {
        if (version_eq(w, a.versions[i])) return true;
    }
    for (std::uint16_t i = 0; i < b.count; ++i) {
        if (version_eq(w, b.versions[i])) return true;
    }
    return false;
}

// ─── canonical state generation via the production assign() path ───

[[nodiscard]] State build_state(Rng& rng) noexcept {
    MV reg;
    const std::uint32_t k = 1u + rng.next_below(4u);  // 1..4 versions
    for (std::uint32_t j = 0; j < k; ++j) {
        Version v{};
        v.value = rng.next_below(3u);
        for (std::size_t n = 0; n < kNodes; ++n) {
            v.clock.entries[n] = rng.next_below(3u);
        }
        (void)reg.assign(LocalV{v});  // canonicalises into the antichain
    }
    return reg.state();
}

struct Spec {
    State a{};
    State b{};
    State c{};
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("crdt_mvregister", cfg,
        [](Rng& rng) noexcept -> Spec {
            return Spec{build_state(rng), build_state(rng), build_state(rng)};
        },
        [](const Spec& spec) noexcept -> bool {
            const State& a = spec.a;
            const State& b = spec.b;
            const State& c = spec.c;

            const State ab = MV::merge(a, b);

            // ── independent Pareto-frontier merge oracle ──
            if (!state_eq_oracle(ab, oracle_merge(a, b))) return false;

            // ── result is a valid antichain (nothing dominates) ──
            if (!is_antichain(ab)) return false;

            // ── containment: no synthesised versions ──
            for (std::uint16_t i = 0; i < ab.count; ++i) {
                if (!in_either(ab.versions[i], a, b)) return false;
            }

            // ── semilattice laws (order-independent set equality) ──
            if (!state_eq(ab, MV::merge(b, a))) return false;          // commutativity
            if (!state_eq(MV::merge(a, a), a)) return false;           // idempotence
            const State left = MV::merge(MV::merge(a, b), c);
            const State right = MV::merge(a, MV::merge(b, c));
            if (!state_eq(left, right)) return false;                  // associativity

            return true;
        });
}
