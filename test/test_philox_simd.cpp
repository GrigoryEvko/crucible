// ═══════════════════════════════════════════════════════════════════
// test_philox_simd — bit-equivalence proof for SIMD-9
//
// Hand-crafted edge cases proving philox_batch8 produces output
// bit-identical to the scalar Philox::generate oracle on every lane.
// Heavier randomized fuzzing lives in
// fuzz/property/prop_philox_simd_equivalence.cpp.
//
// What we cover here:
//   1. All-zero inputs (canonical reference vector)
//   2. All-FF inputs (ditto)
//   3. Mixed counter / mixed key (typical streaming RNG pattern)
//   4. Sequential counters per lane (the actual dropout pattern)
//   5. Heterogeneous keys (different ops in same batch)
//   6. Tip-of-uint32 boundary values
//   7. Cross-lane diffusion: a 1-bit change in lane 0 must NOT
//      affect lanes 1-7 (per-lane independence guarantee)
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Philox.h>
#include <crucible/PhiloxSimd.h>
#include <crucible/safety/Simd.h>

#include <array>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;
using namespace crucible::detail;

namespace {

// ── Helper: build a u32x8 from 8 explicit values ──────────────────
//
// The generator constructor is the only way to construct a vec
// lane-by-lane (operator[] is value-returning const, so subscript
// assignment isn't an option).  Lane index is a compile-time
// integral_constant.

[[nodiscard]] simd::u32x8 vec8(std::array<uint32_t, 8> v) noexcept {
    return simd::u32x8([&](auto lane) noexcept -> uint32_t {
        return v[decltype(lane)::value];
    });
}

// ── Helper: read one lane out of a u32x8 ──────────────────────────
//
// std::simd::vec::operator[] takes __simd_size_type (= int).  Keep
// the helper signature in size_t for parity with std::array, then
// cast explicitly at the subscript site.

[[nodiscard]] uint32_t at(simd::u32x8 v, std::size_t lane) noexcept {
    return v[static_cast<int>(lane)];
}

// ── Oracle: per-lane scalar Philox::generate ──────────────────────
//
// For each lane i, run Philox::generate with the lane's counter and
// key, then verify the SIMD batch result matches lane-by-lane.

void check_equivalence(
    std::array<Philox::Ctr, 8> counters,
    std::array<Philox::Key, 8> keys,
    const char* what) noexcept
{
    // Build SoA inputs.
    auto ctr0 = vec8({counters[0][0], counters[1][0], counters[2][0], counters[3][0],
                      counters[4][0], counters[5][0], counters[6][0], counters[7][0]});
    auto ctr1 = vec8({counters[0][1], counters[1][1], counters[2][1], counters[3][1],
                      counters[4][1], counters[5][1], counters[6][1], counters[7][1]});
    auto ctr2 = vec8({counters[0][2], counters[1][2], counters[2][2], counters[3][2],
                      counters[4][2], counters[5][2], counters[6][2], counters[7][2]});
    auto ctr3 = vec8({counters[0][3], counters[1][3], counters[2][3], counters[3][3],
                      counters[4][3], counters[5][3], counters[6][3], counters[7][3]});
    auto key0 = vec8({keys[0][0], keys[1][0], keys[2][0], keys[3][0],
                      keys[4][0], keys[5][0], keys[6][0], keys[7][0]});
    auto key1 = vec8({keys[0][1], keys[1][1], keys[2][1], keys[3][1],
                      keys[4][1], keys[5][1], keys[6][1], keys[7][1]});

    auto batch = philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);

    for (std::size_t lane = 0; lane < 8; ++lane) {
        auto expected = Philox::generate(counters[lane], keys[lane]);
        uint32_t got0 = at(batch.r0, lane);
        uint32_t got1 = at(batch.r1, lane);
        uint32_t got2 = at(batch.r2, lane);
        uint32_t got3 = at(batch.r3, lane);

        if (got0 != expected[0] || got1 != expected[1] ||
            got2 != expected[2] || got3 != expected[3])
        {
            std::fprintf(stderr,
                "[%s] lane %zu MISMATCH:\n"
                "  expected: %08x %08x %08x %08x\n"
                "       got: %08x %08x %08x %08x\n",
                what, lane,
                expected[0], expected[1], expected[2], expected[3],
                got0, got1, got2, got3);
            std::abort();
        }
    }
}

// ── Test cases ────────────────────────────────────────────────────

void test_all_zeros() {
    std::array<Philox::Ctr, 8> counters{};  // all-zero NSDMI
    std::array<Philox::Key, 8> keys{};
    check_equivalence(counters, keys, "all-zeros");
    std::printf("  test_all_zeros: PASSED\n");
}

void test_all_ones() {
    std::array<Philox::Ctr, 8> counters;
    std::array<Philox::Key, 8> keys;
    for (std::size_t i = 0; i < 8; ++i) {
        counters[i] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        keys[i]     = {0xFFFFFFFFu, 0xFFFFFFFFu};
    }
    check_equivalence(counters, keys, "all-ones");
    std::printf("  test_all_ones: PASSED\n");
}

void test_streaming_counters() {
    // Typical dropout pattern: lane i sees counter (i, key_hi, 0, 0)
    // with the same key broadcast.  Simulates philox-per-element of
    // a tensor row.
    std::array<Philox::Ctr, 8> counters;
    std::array<Philox::Key, 8> keys;
    constexpr Philox::Key shared_key = {0xCAFEBABEu, 0xDEADBEEFu};
    for (std::size_t i = 0; i < 8; ++i) {
        counters[i] = {static_cast<uint32_t>(i), 0, 0, 0};
        keys[i] = shared_key;
    }
    check_equivalence(counters, keys, "streaming-counters");
    std::printf("  test_streaming_counters: PASSED\n");
}

void test_heterogeneous_keys() {
    // Different ops in the same batch — every lane has a different key.
    // Tests that key1 (the "high" half of the 64-bit key) is properly
    // mixed in via the W1 weyl bump.
    std::array<Philox::Ctr, 8> counters;
    std::array<Philox::Key, 8> keys;
    for (std::size_t i = 0; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters[i] = {0x12345678u, 0xAABBCCDDu, 0, 0};
        keys[i] = {0x10000000u + iu * 0x11111111u, 0xFEDCBA98u + iu};
    }
    check_equivalence(counters, keys, "heterogeneous-keys");
    std::printf("  test_heterogeneous_keys: PASSED\n");
}

void test_boundary_values() {
    // Tip-of-uint32 boundary values — the cast-multiply-shift idiom
    // is most likely to misbehave near the wraparound point.
    std::array<Philox::Ctr, 8> counters{
        Philox::Ctr{0u, 0u, 0u, 0u},
        Philox::Ctr{0xFFFFFFFFu, 0u, 0u, 0u},
        Philox::Ctr{0x80000000u, 0x80000000u, 0u, 0u},
        Philox::Ctr{0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu},
        Philox::Ctr{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u},
        Philox::Ctr{1u, 0xFFFFFFFEu, 1u, 0xFFFFFFFEu},
        Philox::Ctr{Philox::M0, Philox::M1, Philox::W0, Philox::W1},  // multiply by self
        Philox::Ctr{0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBADC0DEDu},
    };
    std::array<Philox::Key, 8> keys{
        Philox::Key{0u, 0u},
        Philox::Key{0xFFFFFFFFu, 0u},
        Philox::Key{0u, 0xFFFFFFFFu},
        Philox::Key{0xFFFFFFFFu, 0xFFFFFFFFu},
        Philox::Key{0x80000000u, 0x80000000u},
        Philox::Key{0x7FFFFFFFu, 0x7FFFFFFFu},
        Philox::Key{Philox::M0, Philox::M1},
        Philox::Key{0xA5A5A5A5u, 0x5A5A5A5Au},
    };
    check_equivalence(counters, keys, "boundary-values");
    std::printf("  test_boundary_values: PASSED\n");
}

void test_per_lane_independence() {
    // Per-lane independence: changing lane 0's input bits MUST NOT
    // change any other lane's output.  This is the load-bearing
    // property — without it, the entire SIMD scheme is meaningless.
    //
    // Verify by running TWO batches: first with lane 0 = (0,0,0,0)/(0,0),
    // second with lane 0 = (1,0,0,0)/(0,0).  Lanes 1-7 are identical
    // in both batches with non-zero distinct inputs.

    std::array<Philox::Ctr, 8> counters_a;
    std::array<Philox::Ctr, 8> counters_b;
    std::array<Philox::Key, 8> keys{};

    counters_a[0] = {0, 0, 0, 0};
    counters_b[0] = {1, 0, 0, 0};   // ← only difference
    keys[0] = {0, 0};

    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters_a[i] = {iu * 100u, iu * 200u, iu * 300u, iu * 400u};
        counters_b[i] = counters_a[i];   // ← identical for lanes 1-7
        keys[i] = {iu * 11u, iu * 13u};
    }

    auto build_v = [&](std::array<Philox::Ctr, 8>& c, std::size_t word) {
        return vec8({c[0][word], c[1][word], c[2][word], c[3][word],
                     c[4][word], c[5][word], c[6][word], c[7][word]});
    };
    auto build_k = [&](std::array<Philox::Key, 8>& k, std::size_t word) {
        return vec8({k[0][word], k[1][word], k[2][word], k[3][word],
                     k[4][word], k[5][word], k[6][word], k[7][word]});
    };

    auto a = philox_batch8(build_v(counters_a, 0), build_v(counters_a, 1),
                           build_v(counters_a, 2), build_v(counters_a, 3),
                           build_k(keys, 0),       build_k(keys, 1));
    auto b = philox_batch8(build_v(counters_b, 0), build_v(counters_b, 1),
                           build_v(counters_b, 2), build_v(counters_b, 3),
                           build_k(keys, 0),       build_k(keys, 1));

    // Lane 0 MUST differ (different inputs → different outputs with
    // overwhelming probability).
    assert(at(a.r0, 0) != at(b.r0, 0)
        && "lane 0 must change when its input changes");

    // Lanes 1-7 MUST be identical bit-for-bit.
    for (std::size_t lane = 1; lane < 8; ++lane) {
        assert(at(a.r0, lane) == at(b.r0, lane)
            && "non-perturbed lane must not change");
        assert(at(a.r1, lane) == at(b.r1, lane));
        assert(at(a.r2, lane) == at(b.r2, lane));
        assert(at(a.r3, lane) == at(b.r3, lane));
    }

    std::printf("  test_per_lane_independence: PASSED\n");
}

void test_known_vector_lane0() {
    // Anchor the SIMD path against the locked-in reference vector
    // from Random123 (Salmon et al. 2011).  Lane 0 = all zeros input
    // must produce the canonical {0x6627E8D5, 0xE169C58D, 0xBC57AC4C,
    // 0x9B00DBD8} regardless of what the other 7 lanes contain.

    std::array<Philox::Ctr, 8> counters{};   // all zero
    std::array<Philox::Key, 8> keys{};       // all zero

    // Other lanes get arbitrary noise — must not affect lane 0.
    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters[i] = {0xDEADBEEFu * iu, 0xCAFEBABEu * iu,
                       0xFEEDFACEu * iu, 0xBADC0DEDu * iu};
        keys[i] = {0xA5A5A5A5u * iu, 0x5A5A5A5Au * iu};
    }

    auto ctr0 = vec8({counters[0][0], counters[1][0], counters[2][0], counters[3][0],
                      counters[4][0], counters[5][0], counters[6][0], counters[7][0]});
    auto ctr1 = vec8({counters[0][1], counters[1][1], counters[2][1], counters[3][1],
                      counters[4][1], counters[5][1], counters[6][1], counters[7][1]});
    auto ctr2 = vec8({counters[0][2], counters[1][2], counters[2][2], counters[3][2],
                      counters[4][2], counters[5][2], counters[6][2], counters[7][2]});
    auto ctr3 = vec8({counters[0][3], counters[1][3], counters[2][3], counters[3][3],
                      counters[4][3], counters[5][3], counters[6][3], counters[7][3]});
    auto key0 = vec8({keys[0][0], keys[1][0], keys[2][0], keys[3][0],
                      keys[4][0], keys[5][0], keys[6][0], keys[7][0]});
    auto key1 = vec8({keys[0][1], keys[1][1], keys[2][1], keys[3][1],
                      keys[4][1], keys[5][1], keys[6][1], keys[7][1]});

    auto batch = philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);

    assert(at(batch.r0, 0) == 0x6627E8D5u);
    assert(at(batch.r1, 0) == 0xE169C58Du);
    assert(at(batch.r2, 0) == 0xBC57AC4Cu);
    assert(at(batch.r3, 0) == 0x9B00DBD8u);

    std::printf("  test_known_vector_lane0: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_philox_simd:\n");

    test_all_zeros();
    test_all_ones();
    test_streaming_counters();
    test_heterogeneous_keys();
    test_boundary_values();
    test_per_lane_independence();
    test_known_vector_lane0();

    std::printf("test_philox_simd: ALL PASSED\n");
    return 0;
}
