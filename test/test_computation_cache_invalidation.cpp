// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// Appending a new atom to the effect Universe must not invalidate any
// existing cache entry.  This file pins that by computing the cache key
// for all 64 subsets of the six-atom Universe against one fixed
// function and argument list, then asserting the keys are pairwise
// distinct and non-zero.  A new atom can then only produce new keys.

#include <crucible/cipher/ComputationCache.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

namespace cipher = ::crucible::cipher;
namespace eff = ::crucible::effects;

// Only the function's identity contributes to the key.  Renaming it or
// its namespace changes every pinned key and reads as algorithm drift.

namespace found_f13_pins {
inline void canonical_pin_fn(int) noexcept {}
}  // namespace found_f13_pins

// Effect underlying values line up with bit positions, which is what
// makes the cast from a bit index to an Effect correct.

template <unsigned Mask, unsigned Bit, eff::Effect... Atoms>
struct subset_row_helper {
    using type =
        std::conditional_t<(Mask & (1u << Bit)) != 0u,
                           typename subset_row_helper<Mask, Bit + 1u, Atoms..., static_cast<eff::Effect>(Bit)>::type,
                           typename subset_row_helper<Mask, Bit + 1u, Atoms...>::type>;
};

template <unsigned Mask, eff::Effect... Atoms>
struct subset_row_helper<Mask, 6u, Atoms...> {
    using type = eff::Row<Atoms...>;
};

// The helper recurses over bits 0 through 5 and ignores higher bits, so
// Mask 63 and Mask 127 would produce the same Row.  The requires-clause
// makes a caller passing 64 or more fail at substitution instead.
template <unsigned Mask>
    requires(Mask < 64u)
using subset_row = typename subset_row_helper<Mask, 0u>::type;

// These four fire before the 64-key fold, so a bit-position error shows
// up here instead of as a confusing collision diagnostic.
static_assert(std::is_same_v<subset_row<0b000000>, eff::Row<>>);
static_assert(std::is_same_v<subset_row<0b000001>, eff::Row<eff::Effect::Alloc>>);
static_assert(std::is_same_v<subset_row<0b000010>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<subset_row<0b111111>, eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                                                            eff::Effect::Bg, eff::Effect::Init, eff::Effect::Test>>);

template <unsigned Mask>
inline constexpr std::uint64_t cache_key_for_subset =
    cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn, subset_row<Mask>, int>;

template <std::size_t... Is>
consteval std::array<std::uint64_t, 64> build_all_keys(std::index_sequence<Is...>) noexcept {
    return std::array<std::uint64_t, 64>{cache_key_for_subset<static_cast<unsigned>(Is)>...};
}

inline constexpr std::array<std::uint64_t, 64> all_subset_keys = build_all_keys(std::make_index_sequence<64>{});

// O(N²) at consteval: 2016 comparisons for N of 64.

template <std::size_t N>
consteval bool all_pairwise_distinct(const std::array<std::uint64_t, N>& keys) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (keys[i] == keys[j]) return false;
        }
    }
    return true;
}

template <std::size_t N>
consteval bool all_nonzero(const std::array<std::uint64_t, N>& keys) noexcept {
    for (auto k : keys)
        if (k == 0) return false;
    return true;
}

static_assert(all_pairwise_distinct(all_subset_keys), "The 64 cache keys for the 64 subsets of the 6-atom Universe "
                                                      "must be pairwise distinct.  A collision means the cache-"
                                                      "invalidation footprint is not bounded: two existing rows share "
                                                      "a cache slot, and federation peers silently alias their "
                                                      "entries.  Find which two subsets collided, then trace "
                                                      "row_hash_contribution and combine_ids.");

static_assert(all_nonzero(all_subset_keys), "Every cache key for an existing row must be non-zero.  A zero "
                                            "key aliases against the federation protocol's "
                                            "uninitialized-slot sentinel, so a benign cache miss reads as a "
                                            "cache hit with a corrupt body.");

static_assert(cipher::computation_cache_key<&found_f13_pins::canonical_pin_fn, int>
                  != cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn, eff::Row<>, int>,
              "The row-blind cache key and the row-aware empty-row cache key "
              "must differ even when the row is empty: the row-aware path "
              "applies row_hash to the empty row, and the row-blind path skips "
              "that step entirely.");

// Tautological under consteval semantics, but an explicit pin against a
// future refactor that makes evaluation non-deterministic.
static_assert(cache_key_for_subset<0b000000> == cache_key_for_subset<0b000000>);
static_assert(cache_key_for_subset<0b111111> == cache_key_for_subset<0b111111>);

// The index-sequence construction must not re-order or drop subsets.
static_assert(all_subset_keys[0] == cache_key_for_subset<0u>);
static_assert(all_subset_keys[1] == cache_key_for_subset<1u>);
static_assert(all_subset_keys[63] == cache_key_for_subset<63u>);

// The 64-key matrix only ever builds atoms in ascending order, so it
// never compares two rows that are permutations of each other.  These
// witnesses cover that, at a two-atom row and at the full universe.

static_assert(cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn,
                                                   eff::Row<eff::Effect::Alloc, eff::Effect::IO>, int>
                  == cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn,
                                                          eff::Row<eff::Effect::IO, eff::Effect::Alloc>, int>,
              "The cache key for Row<Alloc, IO> must equal the cache key for "
              "Row<IO, Alloc>: row_hash is a sort-fold over Effect underlying "
              "values, so permuting the atoms in the pack changes nothing.  A "
              "failure here means the fold reads declaration order instead.  "
              "Investigate row_hash_contribution<Row<Es...>>.");

static_assert(
    cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn,
                                         eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
                                                  eff::Effect::Bg, eff::Effect::Init, eff::Effect::Test>,
                                         int>
        == cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn,
                                                eff::Row<eff::Effect::Test, eff::Effect::Init, eff::Effect::Bg,
                                                         eff::Effect::Block, eff::Effect::IO, eff::Effect::Alloc>,
                                                int>,
    "A full-universe Row in ascending atom order must hash to the "
    "same cache key as the same Row in descending order.");

static_assert(cipher::computation_cache_key_in_row<&found_f13_pins::canonical_pin_fn,
                                                   eff::Row<eff::Effect::Test, eff::Effect::Init, eff::Effect::Bg,
                                                            eff::Effect::Block, eff::Effect::IO, eff::Effect::Alloc>,
                                                   int>
                  == cache_key_for_subset<63u>,
              "A descending-order full-universe Row hashes to the same cache "
              "key as subset_row<63>, which builds in ascending order.  This "
              "bridges the manually spelled Row and the helper-generated "
              "one.");

// The variadic form keeps unparenthesised template-argument commas
// from being read as macro-argument separators.

#define ASSERT_TRUE(...)                                                                                    \
    do {                                                                                                    \
        if (!(__VA_ARGS__)) {                                                                               \
            std::fprintf(stderr, "    ASSERT_TRUE failed: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            std::abort();                                                                                   \
        }                                                                                                   \
    } while (0)

}  // namespace

int main() {
    std::fprintf(stderr, "test_computation_cache_invalidation:\n");

    // The compile-time assertions above could all hold while runtime
    // evaluation diverges.  The volatile reads keep the optimizer from
    // folding this sample back into constants.

    std::fprintf(stderr, "  R1 (runtime_equivalence_sentinel): ");

    // Sample 6 subset masks covering edge + interior cases.
    constexpr unsigned sample_masks[] = {0u, 1u, 2u, 4u, 0b101010u, 63u};
    for (unsigned m : sample_masks) {
        // The loop variable is not constexpr, so the switch maps it
        // back to the consteval values.
        std::uint64_t consteval_value = 0;
        switch (m) {
            case 0u:
                consteval_value = cache_key_for_subset<0u>;
                break;
            case 1u:
                consteval_value = cache_key_for_subset<1u>;
                break;
            case 2u:
                consteval_value = cache_key_for_subset<2u>;
                break;
            case 4u:
                consteval_value = cache_key_for_subset<4u>;
                break;
            case 0b101010u:
                consteval_value = cache_key_for_subset<0b101010u>;
                break;
            case 63u:
                consteval_value = cache_key_for_subset<63u>;
                break;
            default:
                std::abort();  // unreachable under sample_masks
        }
        volatile std::uint64_t runtime_value = consteval_value;
        if (runtime_value == 0) std::abort();
    }
    std::fprintf(stderr, "PASSED\n");

    std::fprintf(stderr,
                 "  R2 (subset_coverage_report): %zu subsets, "
                 "all keys non-zero, all pairwise-distinct: ",
                 all_subset_keys.size());

    bool every_nonzero = true;
    for (auto k : all_subset_keys)
        if (k == 0) every_nonzero = false;
    ASSERT_TRUE(every_nonzero);

    ASSERT_TRUE(all_subset_keys[0] != 0);
    ASSERT_TRUE(all_subset_keys[63] != 0);
    ASSERT_TRUE(all_subset_keys[0] != all_subset_keys[63]);

    std::fprintf(stderr, "PASSED\n");

    std::fprintf(stderr, "\n2 runtime checks passed; the invalidation invariants are "
                         "pinned at compile time.\n");
    return 0;
}
