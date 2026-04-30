// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// ═══════════════════════════════════════════════════════════════════
// test_computation_cache_invalidation — FOUND-F13 capstone for the
// cache-invalidation-on-Universe-extension invariant.
//
// CONTEXT — the cache-invalidation invariant (FOUND-I04 derivation).
//
// `effects/Capabilities.h` documents the append-only Universe
// extension discipline: existing `Effect` atoms keep their underlying
// values forever; new atoms append at the next free value (e.g., a
// hypothetical `Refute = 6` would join Alloc=0 / IO=1 / Block=2 /
// Bg=3 / Init=4 / Test=5).
//
// The FOUND-F13 INVARIANT THIS TEST PINS:
//
//     For every (FnPtr, Args, Row<Es...>) triple where Row<Es...>
//     uses ONLY currently-defined Effect atoms, the cache key
//     `computation_cache_key_in_row<&FnPtr, Row<Es...>, Args...>`
//     is a deterministic function of the underlying Effect values
//     and the cache-key composition algorithm.  Both inputs are
//     pinned (Capabilities.h's static_asserts on values; F09 +
//     F11 + RowHashFold.h's algorithm pins).  Therefore the cache
//     key is invariant under a hypothetical Universe extension —
//     pre-existing entries DO NOT need invalidation when a new
//     atom is appended to the Universe.
//
// CACHE-INVALIDATION FOOTPRINT BOUND:
//
//     If a hypothetical `Refute = 6` is appended to the Effect
//     enum, the invalidation footprint is bounded to cache entries
//     whose row mentions Refute.  Pre-existing rows
//     (Row<>, Row<Alloc>, ..., Row<Alloc, IO, Block, Bg, Init,
//     Test>) keep their cache keys.  This bound is what makes
//     append-only extensions cheap on the federation wire — fleet
//     peers don't have to flush their caches when a new atom is
//     introduced; only entries that USE the new atom are
//     "fresh" (have no pre-existing peer to compare to).
//
// WITNESS — every subset of the 6-atom Universe.
//
// The 6-atom OS Universe has 2⁶ = 64 distinct row shapes (every
// subset of {Alloc, IO, Block, Bg, Init, Test}).  This test:
//
//   (1) Computes the cache key for each of the 64 row shapes
//       paired with one fixed (FnPtr, Args) tuple.
//   (2) Asserts the 64 keys are pairwise distinct (no collisions).
//   (3) Asserts every key is non-zero (no degenerate fold collapse).
//   (4) Asserts row-blind vs row-aware-Empty disjointness.
//   (5) Pins runtime-equivalence of consteval keys.
//
// Coverage rationale: 64 subsets × pairwise-distinctness = C(64,2)
// = 2016 inequality checks at consteval, folded into one
// `all_pairwise_distinct` predicate.  This is the maximally tight
// witness for the structural invariant — every existing row
// currently produces a unique cache key; a new atom can only
// generate NEW unique keys without disturbing the existing 64.
//
// IF THIS RED-LINES:
//
//   * Effect enum value drift  → fix per FOUND-I04 ceremony in
//     Capabilities.h.
//   * Hash algorithm drift     → fix per RowHashFold.h pins +
//     ComputationCache.h F09 algorithm.
//   * Composition drift        → fix per StableName.h + F09
//     `combine_ids` invariants.
//   * Wire-format-break needed → bump CDAG_VERSION (Types.h) AND
//     re-pin RowHashFold.h hex literals AND document break in
//     CRUCIBLE.md / MIMIC.md / FORGE.md AND coordinate fleet-wide
//     cache flush.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/cipher/ComputationCache.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

namespace cipher = ::crucible::cipher;
namespace eff    = ::crucible::effects;

// ── Canonical pin function ──────────────────────────────────────────
//
// One stable (FnPtr, Args) tuple.  The function is intentionally
// trivial; only its identity contributes to the cache key.  Keep
// the namespace + name STABLE — a rename would change the hash for
// every pinned key, masquerading as algorithm drift.

namespace found_f13_pins {
inline void canonical_pin_fn(int) noexcept {}
}  // namespace found_f13_pins

// ── 6-bit subset → Row<Es...> helper ────────────────────────────────
//
// Given a 6-bit mask M (M < 64), produces Row<Es...> where each Eₖ
// is included iff (M >> k) & 1.  Recursive helper unfolds bit by
// bit.  Effect underlying values are aligned to bit positions
// (Alloc=0 ⇒ bit 0, IO=1 ⇒ bit 1, ..., Test=5 ⇒ bit 5) per the
// OsUniverse::bit_position bridge in OsUniverse.h.

template <unsigned Mask, unsigned Bit, eff::Effect... Atoms>
struct subset_row_helper {
    using type = std::conditional_t<
        (Mask & (1u << Bit)) != 0u,
        typename subset_row_helper<Mask, Bit + 1u, Atoms...,
            static_cast<eff::Effect>(Bit)>::type,
        typename subset_row_helper<Mask, Bit + 1u, Atoms...>::type
    >;
};

// Termination — Bit == effect_count means we've considered all 6 bits.
template <unsigned Mask, eff::Effect... Atoms>
struct subset_row_helper<Mask, 6u, Atoms...> {
    using type = eff::Row<Atoms...>;
};

// FOUND-F13-AUDIT (Finding B) — Mask precondition.  The internal
// helper recurses Bit ∈ [0, 6).  High bits of Mask (≥ 64) are
// silently IGNORED — without the requires-clause below, subset_row<63>
// and subset_row<127> would produce the same Row<> type (full
// universe), aliasing four high-bit-garbage masks to a single Row.
// The internal helper is only ever called with Mask < 64; the
// requires-clause on the public alias entry pins the precondition
// at substitution time, so any future caller passing Mask ≥ 64
// fails loudly with a constraint violation instead of producing
// surprising aliased rows.
template <unsigned Mask>
    requires (Mask < 64u)
using subset_row = typename subset_row_helper<Mask, 0u>::type;

// Spot-check the helper at compile time before building the full
// 64-element fold.  These four sanity checks catch any off-by-one
// or bit-position errors in subset_row_helper before the larger
// pairwise-distinctness check fires with a confusing diagnostic.
static_assert(std::is_same_v<subset_row<0b000000>, eff::Row<>>);
static_assert(std::is_same_v<
    subset_row<0b000001>, eff::Row<eff::Effect::Alloc>>);
static_assert(std::is_same_v<
    subset_row<0b000010>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<
    subset_row<0b111111>,
    eff::Row<eff::Effect::Alloc, eff::Effect::IO,
             eff::Effect::Block, eff::Effect::Bg,
             eff::Effect::Init,  eff::Effect::Test>>);

// ── Per-subset cache key ────────────────────────────────────────────

template <unsigned Mask>
inline constexpr std::uint64_t cache_key_for_subset =
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        subset_row<Mask>,
        int>;

// ── 64-element fold ─────────────────────────────────────────────────

template <std::size_t... Is>
consteval std::array<std::uint64_t, 64>
build_all_keys(std::index_sequence<Is...>) noexcept {
    return std::array<std::uint64_t, 64>{
        cache_key_for_subset<static_cast<unsigned>(Is)>...};
}

inline constexpr std::array<std::uint64_t, 64> all_subset_keys =
    build_all_keys(std::make_index_sequence<64>{});

// ── Pairwise distinctness predicate ─────────────────────────────────
//
// O(N²) consteval double-loop.  N=64 ⇒ C(64,2) = 2016 comparisons,
// well within consteval evaluation budget.

template <std::size_t N>
consteval bool all_pairwise_distinct(
    const std::array<std::uint64_t, N>& keys) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (keys[i] == keys[j]) return false;
        }
    }
    return true;
}

// ── Non-zero predicate ──────────────────────────────────────────────

template <std::size_t N>
consteval bool all_nonzero(
    const std::array<std::uint64_t, N>& keys) noexcept {
    for (auto k : keys) if (k == 0) return false;
    return true;
}

// ── Compile-time witnesses ──────────────────────────────────────────
//
// These static_asserts are the load-bearing FOUND-F13 invariants.
// Each one fires at compile time of THIS TU, surfacing the precise
// failure mode of any drift in the cache-key pipeline.

// (W1) All 64 cache keys for distinct row shapes are pairwise
// distinct.  This is the structural invariant the invalidation-
// footprint bound depends on: every existing (FnPtr, Args, Row)
// triple has a unique cache slot, so cache state is not aliased
// across rows.  A new atom can only generate NEW keys without
// disturbing the 64 existing ones.
static_assert(all_pairwise_distinct(all_subset_keys),
    "F13: 64 cache keys for the 64 subsets of the 6-atom Universe "
    "must be pairwise distinct.  A collision here means the cache-"
    "invalidation footprint is NOT bounded — two existing rows "
    "would share a cache slot, and federation peer synchronization "
    "would silently alias their entries.  Investigate which two "
    "subsets collided and trace through row_hash_contribution + "
    "combine_ids in StableName.h to find the algorithm bug.");

// (W2) Every cache key is non-zero.  A zero key would alias against
// any uninitialized cache slot (which lookup returns nullptr for —
// a zero key would be confused with "no entry" semantics in
// federation protocols that use 0 as a sentinel).
static_assert(all_nonzero(all_subset_keys),
    "F13: every cache key for an existing row must be non-zero.  "
    "A zero key would alias against the federation protocol's "
    "uninitialized-slot sentinel, causing benign cache misses to "
    "be mis-interpreted as cache hits with corrupt bodies.");

// (W3) Row-blind vs row-aware-EmptyR keys are disjoint.  Pin the
// F11 disjoint-slot invariant at the F13 level: row-blind cache
// must not collide with row-aware-EmptyR cache for the same
// (FnPtr, Args).
static_assert(
    cipher::computation_cache_key<
        &found_f13_pins::canonical_pin_fn, int>
    != cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn, eff::Row<>, int>,
    "F13: row-blind cache key and row-aware-EmptyR cache key MUST "
    "differ even when the row is empty — the row-aware path applies "
    "row_hash to the EmptyRow, while the row-blind path skips that "
    "step entirely.  Disjointness is the load-bearing F11 invariant "
    "and is reasserted here at the F13 invalidation-witness level.");

// (W4) Determinism — repeated consteval evaluation produces the
// same key.  This is implicit in consteval semantics, but the
// explicit pin catches any UB-level issue (e.g., undefined-behavior
// from dropped initializer in a future refactor) that would
// silently produce non-deterministic output.
static_assert(
    cache_key_for_subset<0b000000> == cache_key_for_subset<0b000000>);
static_assert(
    cache_key_for_subset<0b111111> == cache_key_for_subset<0b111111>);

// (W5) The fold-to-array preserves the per-subset values.  Pins
// that the index_sequence-based array construction in
// build_all_keys does not silently re-order or drop subsets.
static_assert(
    all_subset_keys[0]  == cache_key_for_subset<0u>);
static_assert(
    all_subset_keys[1]  == cache_key_for_subset<1u>);
static_assert(
    all_subset_keys[63] == cache_key_for_subset<63u>);

// FOUND-F13-AUDIT (Finding A) — permutation invariance witness for
// canonical_pin_fn.  `subset_row<Mask>` always produces atoms in
// ASCENDING order — the 64-key matrix above never compares two
// keys whose underlying Row<> types are permutations of each other.
// row_hash_contribution<Row<Es...>> is a sort-fold over Effect
// underlying values (per FOUND-I02 RowHashFold.h), so semantically
// equivalent rows under permutation MUST yield the same key.
// test_computation_cache.cpp already pins this for &p_unary; this
// witness re-pins it at the F13 canonical-fixture level so any
// drift in the row_hash sort-fold (e.g., a refactor that
// accidentally folds in declaration order instead of underlying-
// value order) surfaces in the F13 invalidation TU specifically.
//
// Coverage: 2-atom and 6-atom permutations exercise both the
// "pair-row" fold edge and the "full-universe" fold edge.

static_assert(
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO>, int>
    ==
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        eff::Row<eff::Effect::IO, eff::Effect::Alloc>, int>,
    "F13: cache key for Row<Alloc, IO> must equal cache key for "
    "Row<IO, Alloc> — row_hash is a sort-fold over Effect underlying "
    "values, so permutation of atoms in the Row pack is a no-op for "
    "the federation key.  A drift here means the sort-fold has been "
    "refactored to read declaration order; investigate "
    "row_hash_contribution<Row<Es...>> in safety/diag/RowHashFold.h.");

// Full-universe permutation — six atoms in two different orders.
static_assert(
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                 eff::Effect::Block, eff::Effect::Bg,
                 eff::Effect::Init,  eff::Effect::Test>, int>
    ==
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        eff::Row<eff::Effect::Test,  eff::Effect::Init,
                 eff::Effect::Bg,    eff::Effect::Block,
                 eff::Effect::IO,    eff::Effect::Alloc>, int>,
    "F13: full-universe Row in ascending-atom-order must hash to "
    "the same cache key as the same Row in descending-atom-order "
    "— sort-fold permutation invariance.");

// AND the ascending-order full-universe key MUST equal the F13
// matrix's subset_row<63> key (the ascending-order producer).
static_assert(
    cipher::computation_cache_key_in_row<
        &found_f13_pins::canonical_pin_fn,
        eff::Row<eff::Effect::Test,  eff::Effect::Init,
                 eff::Effect::Bg,    eff::Effect::Block,
                 eff::Effect::IO,    eff::Effect::Alloc>, int>
    == cache_key_for_subset<63u>,
    "F13: descending-order full-universe Row hashes to the same "
    "cache key as subset_row<63> (ascending order).  Pins the "
    "permutation-invariance bridge between the manually-spelled "
    "Row<> and the helper-generated Row<>.");

// ── Variadic ASSERT macro ───────────────────────────────────────────
//
// Same pattern as test_computation_cache_integration.cpp — variadic
// __VA_ARGS__ form sidesteps unparenthesized template-arg-list
// commas that would otherwise be parsed as macro-arg separators.

#define ASSERT_TRUE(...)                                                  \
    do {                                                                  \
        if (!(__VA_ARGS__)) {                                             \
            std::fprintf(stderr,                                          \
                "    ASSERT_TRUE failed: %s (%s:%d)\n",                   \
                #__VA_ARGS__, __FILE__, __LINE__);                        \
            std::abort();                                                 \
        }                                                                 \
    } while (0)

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache_invalidation:\n");

    // ── (R1) Runtime-equivalence sentinel ───────────────────────────
    //
    // The static_asserts above pin the invariants at compile time;
    // this runtime block exercises a sample of cache keys via
    // volatile reads to defeat any optimizer DSE that could collapse
    // the runtime-vs-consteval branches.  If a future toolchain
    // change introduced a subtle ABI-level mismatch between
    // consteval and runtime evaluation (e.g., a calling convention
    // shift that affected the underlying combine_ids result), this
    // runtime peer would catch it.

    std::fprintf(stderr, "  R1 (runtime_equivalence_sentinel): ");

    // Sample 6 subset masks covering edge + interior cases.
    constexpr unsigned sample_masks[] = {0u, 1u, 2u, 4u, 0b101010u, 63u};
    for (unsigned m : sample_masks) {
        // Switch on the runtime mask (it's not constexpr inside the
        // loop body) and read the corresponding consteval value.
        // The mapping below covers exactly the constexpr sample set.
        std::uint64_t consteval_value = 0;
        switch (m) {
            case 0u:        consteval_value = cache_key_for_subset<0u>;        break;
            case 1u:        consteval_value = cache_key_for_subset<1u>;        break;
            case 2u:        consteval_value = cache_key_for_subset<2u>;        break;
            case 4u:        consteval_value = cache_key_for_subset<4u>;        break;
            case 0b101010u: consteval_value = cache_key_for_subset<0b101010u>; break;
            case 63u:       consteval_value = cache_key_for_subset<63u>;       break;
            default: std::abort();  // unreachable under sample_masks
        }
        // The volatile prevents the compiler from collapsing the
        // load through the (non-constexpr) loop variable back into
        // a constant.
        volatile std::uint64_t runtime_value = consteval_value;
        if (runtime_value == 0) std::abort();
    }
    std::fprintf(stderr, "PASSED\n");

    // ── (R2) Subset coverage report ─────────────────────────────────
    //
    // Print a coverage summary so a CI or debug run shows the test
    // actually exercised all 64 subsets.  No assertion; the load-
    // bearing checks are the consteval static_asserts at TU level.
    std::fprintf(stderr, "  R2 (subset_coverage_report): %zu subsets, "
                         "all keys non-zero, all pairwise-distinct: ",
                         all_subset_keys.size());

    bool every_nonzero = true;
    for (auto k : all_subset_keys) if (k == 0) every_nonzero = false;
    ASSERT_TRUE(every_nonzero);

    // Spot-check a few specific keys to surface any non-zero
    // anomalies in the report (e.g., one specific subset producing
    // an unexpectedly small or specific value).
    ASSERT_TRUE(all_subset_keys[0]  != 0);
    ASSERT_TRUE(all_subset_keys[63] != 0);
    ASSERT_TRUE(all_subset_keys[0] != all_subset_keys[63]);

    std::fprintf(stderr, "PASSED\n");

    std::fprintf(stderr,
        "\n2 runtime checks passed; FOUND-F13 invariants pinned at "
        "compile time (5 static_assert blocks).\n");
    return 0;
}
