// ═══════════════════════════════════════════════════════════════════
// prop_crdt_counter.cpp — semilattice-law + read-projection fuzzer for
// the three remaining state-based CRDTs in canopy/Crdt.h that had no
// property coverage: LwwRegister (last-write-wins register), GCounter
// (grow-only counter), and PNCounter (positive-negative counter).
//
// GSet (prop_crdt_gset) and OrSet (prop_crdt_orset) are already fuzzed.
// This closes the simple-CRDT gap; MVRegister (vector-clock multi-value
// register) is the genuinely harder remaining one — left for its own
// pass.  test_crdt.cpp pins hand-picked cases for all three; no property
// fuzzer existed.
//
// All three converge in canopy gossip ONLY if their state merge is a
// join-semilattice op (commutative / associative / idempotent / LUB).  A
// single asymmetry or a dropped slot splits the fleet's replicated state
// permanently.  Each sub-property re-derives the answer with a DIFFERENT
// computation than the code under test, so a divergence is a real bug:
//
//   • crdt_lww  — LwwRegister<u32,u64> merge is a max over the total
//     order (has_value, clock, value).  Production hand-rolls it with
//     <=> + is_lt/is_gt + a manual value tie-break; the oracle re-derives
//     it as std::tuple/std::pair lexicographic compare.  Bottom states
//     (has_value=false) are generated only in canonical form {false,0,0}
//     — the sole shape production ever materialises (default ctor /
//     merge propagation) — so all bottoms are equal and the laws hold
//     under the defaulted operator==.  Per (a,b,c): exact oracle match,
//     commutativity, idempotence, associativity, result-is-an-operand,
//     and key-dominance (the merge result's order key ≥ both inputs').
//
//   • crdt_gcounter — GCounter<4> merge is per-replica max.  Production
//     uses `if (a<b) a=b`; the oracle uses std::max per slot.  value() is
//     a saturating sum (chained detail::sat_add); the oracle re-derives
//     it with an unsigned __int128 accumulate clamped to UINT64_MAX, and
//     is fed the state via a gossip-merge from zero (merge(0,target) ==
//     target).  Per (a,b,c): exact merge oracle, commutativity,
//     idempotence, associativity, per-slot dominance, and value() match.
//
//   • crdt_pncounter — PNCounter<4> merge is two independent GCounter
//     merges (positive, negative).  value() = clamp(Σpos − Σneg) into
//     int64 with both sums first saturated to UINT64_MAX; the oracle
//     re-derives it with a signed __int128 difference clamped to
//     [INT64_MIN, INT64_MAX].  Counts are corner-biased toward
//     UINT64_MAX so the int64 clamp boundaries are actually exercised.
//     Per (a,b,c): exact merge oracle, commutativity, idempotence,
//     associativity, and value() match.
//
// MaxReplicas is 4 — small enough for tight loops, large enough that a
// saturating sum overflows uint64 (4 × ~2⁶⁴) and drives the value()
// clamps.  This is the convergence-safety net the unit tests lacked.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <array>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::uint64_t kU64Max =
    std::numeric_limits<std::uint64_t>::max();
inline constexpr std::size_t kReplicas = 4;

// 128-bit accumulators for the independent saturation oracles.  GCC's
// __int128 is a GNU extension; `__extension__` silences -Wpedantic the
// same way fuzz/property/prop_checked_arith.cpp does.
__extension__ using w_s = __int128;
__extension__ using w_u = unsigned __int128;

// ─── corner-biased generators ──────────────────────────────────────

[[nodiscard]] std::uint64_t gen_clock(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return rng.next_below(4u);  // tiny cluster → equal-clock ties
        case 3: return kU64Max;
        case 4: return kU64Max - 1u;
        default: return rng.next64();
    }
}

[[nodiscard]] std::uint32_t gen_value(Rng& rng) noexcept {
    switch (rng.next_below(4u)) {
        case 0: return 0u;
        case 1: return rng.next_below(4u);  // tiny → equal-value tie-break path
        case 2: return rng.next32() & 0xFFu;
        default: return rng.next32();
    }
}

[[nodiscard]] std::uint64_t gen_count(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return rng.next_below(1000u);          // small
        case 3: return kU64Max;                        // saturate
        case 4: return kU64Max - rng.next_below(8u);   // near-max
        default: return rng.next64();                  // full random
    }
}

// ─── LwwRegister<u32, u64> ─────────────────────────────────────────

using Lww = cc::LwwRegister<std::uint32_t, std::uint64_t>;
using LwwState = Lww::state_type;
using LwwKey = std::tuple<bool, std::uint64_t, std::uint32_t>;

struct LwwOne {
    bool has_value = false;
    std::uint64_t clock = 0;
    std::uint32_t value = 0;
};
struct LwwSpec {
    LwwOne a{};
    LwwOne b{};
    LwwOne c{};
};

[[nodiscard]] LwwOne gen_lww_one(Rng& rng) noexcept {
    // ~1/5 canonical bottom; production never makes a non-canonical one.
    if (rng.next_below(5u) == 0u) {
        return LwwOne{.has_value = false, .clock = 0u, .value = 0u};
    }
    return LwwOne{.has_value = true,
                  .clock = gen_clock(rng),
                  .value = gen_value(rng)};
}

[[nodiscard]] LwwState lww_state(const LwwOne& o) noexcept {
    return LwwState{.value = o.value, .clock = o.clock, .has_value = o.has_value};
}

// Independent total-order key (bottom collapses to the single ⊥).
[[nodiscard]] LwwKey lww_key(const LwwState& s) noexcept {
    return s.has_value ? LwwKey{true, s.clock, s.value} : LwwKey{false, 0u, 0u};
}

// Independent merge oracle: max over the total order, tie → second arg —
// a different formulation from production's <=> chain + value tie-break.
[[nodiscard]] LwwState lww_oracle(const LwwState& a, const LwwState& b) noexcept {
    if (!a.has_value) return b;
    if (!b.has_value) return a;
    const std::pair<std::uint64_t, std::uint32_t> ka{a.clock, a.value};
    const std::pair<std::uint64_t, std::uint32_t> kb{b.clock, b.value};
    return ka > kb ? a : b;
}

// ─── GCounter<kReplicas> ───────────────────────────────────────────

using GC = cc::GCounter<kReplicas>;
using GCState = GC::state_type;
using GCGossip = GC::gossiped_state_type;

struct GCounts {
    std::array<std::uint64_t, kReplicas> v{};
};

[[nodiscard]] GCounts gen_counts(Rng& rng) noexcept {
    GCounts c{};
    for (std::size_t i = 0; i < kReplicas; ++i) {
        c.v[i] = gen_count(rng);
    }
    return c;
}

[[nodiscard]] GCState gc_build(const GCounts& c) noexcept {
    GCState s{};
    for (std::size_t i = 0; i < kReplicas; ++i) {
        s.counts[i] = c.v[i];
    }
    return s;
}

// Independent per-slot-max merge oracle (std::max, not the hand-rolled if).
[[nodiscard]] GCState gc_oracle(const GCState& a, const GCState& b) noexcept {
    GCState r{};
    for (std::size_t i = 0; i < kReplicas; ++i) {
        r.counts[i] = std::max(a.counts[i], b.counts[i]);
    }
    return r;
}

// Independent saturating sum oracle (128-bit accumulate clamped to u64).
[[nodiscard]] std::uint64_t gc_sum(const GCState& s) noexcept {
    w_u acc = 0;
    for (std::size_t i = 0; i < kReplicas; ++i) {
        acc += static_cast<w_u>(s.counts[i]);
    }
    const w_u cap = static_cast<w_u>(kU64Max);
    return acc > cap ? kU64Max : static_cast<std::uint64_t>(acc);
}

struct GCSpec {
    GCounts a{};
    GCounts b{};
    GCounts c{};
};

// ─── PNCounter<kReplicas> ──────────────────────────────────────────

using PC = cc::PNCounter<kReplicas>;
using PCState = PC::state_type;
using PCGossip = PC::gossiped_state_type;

struct PCSpec {
    GCounts ap{}, an{};
    GCounts bp{}, bn{};
    GCounts cp{}, cn{};
};

[[nodiscard]] PCState pc_build(const GCounts& pos, const GCounts& neg) noexcept {
    return PCState{.positive = gc_build(pos), .negative = gc_build(neg)};
}

[[nodiscard]] PCState pc_oracle(const PCState& a, const PCState& b) noexcept {
    return PCState{.positive = gc_oracle(a.positive, b.positive),
                   .negative = gc_oracle(a.negative, b.negative)};
}

// Independent value() oracle: signed 128-bit difference of the two
// saturated sums, clamped into int64.
[[nodiscard]] std::int64_t pc_value_oracle(const PCState& s) noexcept {
    const std::uint64_t pos = gc_sum(s.positive);
    const std::uint64_t neg = gc_sum(s.negative);
    w_s diff = static_cast<w_s>(pos) - static_cast<w_s>(neg);
    const w_s hi = static_cast<w_s>(std::numeric_limits<std::int64_t>::max());
    const w_s lo = static_cast<w_s>(std::numeric_limits<std::int64_t>::min());
    if (diff > hi) diff = hi;
    if (diff < lo) diff = lo;
    return static_cast<std::int64_t>(diff);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    int rc = 0;

    // ── LwwRegister: total-order max semilattice ──
    rc |= run("crdt_lww", cfg,
        [](Rng& rng) noexcept -> LwwSpec {
            return LwwSpec{gen_lww_one(rng), gen_lww_one(rng), gen_lww_one(rng)};
        },
        [](const LwwSpec& spec) noexcept -> bool {
            const LwwState a = lww_state(spec.a);
            const LwwState b = lww_state(spec.b);
            const LwwState c = lww_state(spec.c);

            const LwwState ab = Lww::merge(a, b);

            if (!(ab == lww_oracle(a, b))) return false;          // exact oracle
            if (!(ab == Lww::merge(b, a))) return false;          // commutativity
            if (!(Lww::merge(a, a) == a)) return false;           // idempotence
            if (!(Lww::merge(Lww::merge(a, b), c) ==
                  Lww::merge(a, Lww::merge(b, c)))) return false;  // associativity
            if (!(ab == a || ab == b)) return false;              // picks an operand

            const LwwKey ka = lww_key(a);
            const LwwKey kb = lww_key(b);
            const LwwKey kab = lww_key(ab);
            if (kab < ka || kab < kb) return false;               // LUB dominance
            return true;
        });

    // ── GCounter: per-replica max semilattice + saturating value() ──
    rc |= run("crdt_gcounter", cfg,
        [](Rng& rng) noexcept -> GCSpec {
            return GCSpec{gen_counts(rng), gen_counts(rng), gen_counts(rng)};
        },
        [](const GCSpec& spec) noexcept -> bool {
            const GCState a = gc_build(spec.a);
            const GCState b = gc_build(spec.b);
            const GCState c = gc_build(spec.c);

            const GCState ab = GC::merge(a, b);

            if (!(ab == gc_oracle(a, b))) return false;           // exact oracle
            if (!(ab == GC::merge(b, a))) return false;           // commutativity
            if (!(GC::merge(a, a) == a)) return false;            // idempotence
            if (!(GC::merge(GC::merge(a, b), c) ==
                  GC::merge(a, GC::merge(b, c)))) return false;    // associativity
            for (std::size_t i = 0; i < kReplicas; ++i) {
                if (ab.counts[i] < a.counts[i]) return false;     // dominance
                if (ab.counts[i] < b.counts[i]) return false;
            }

            // value(): gossip-merge `a` into a fresh zero counter, so its
            // state becomes exactly `a`, then check the saturating sum.
            GC counter;
            (void)counter.merge(GCGossip{a});
            if (counter.value() != gc_sum(a)) return false;
            return true;
        });

    // ── PNCounter: paired GCounter merges + clamped signed value() ──
    rc |= run("crdt_pncounter", cfg,
        [](Rng& rng) noexcept -> PCSpec {
            return PCSpec{gen_counts(rng), gen_counts(rng),
                          gen_counts(rng), gen_counts(rng),
                          gen_counts(rng), gen_counts(rng)};
        },
        [](const PCSpec& spec) noexcept -> bool {
            const PCState a = pc_build(spec.ap, spec.an);
            const PCState b = pc_build(spec.bp, spec.bn);
            const PCState c = pc_build(spec.cp, spec.cn);

            const PCState ab = PC::merge(a, b);

            if (!(ab == pc_oracle(a, b))) return false;           // exact oracle
            if (!(ab == PC::merge(b, a))) return false;           // commutativity
            if (!(PC::merge(a, a) == a)) return false;            // idempotence
            if (!(PC::merge(PC::merge(a, b), c) ==
                  PC::merge(a, PC::merge(b, c)))) return false;    // associativity

            PC counter;
            (void)counter.merge(PCGossip{a});
            if (counter.value() != pc_value_oracle(a)) return false;
            return true;
        });

    return rc;
}
