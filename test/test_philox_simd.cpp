// The eight-lane generator must produce, in every lane, exactly the
// bits the scalar generator produces for that lane's inputs.  The
// cases below are chosen by hand to sit at the edges where a lane
// could diverge.  Random coverage of the same claim is fuzzed
// separately.

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

// Subscripting a vector returns a value rather than a reference, so a
// lane cannot be assigned through it.  Building lane by lane goes
// through the generator constructor instead, and the lane index it
// passes is a compile-time constant.
[[nodiscard]] simd::u32x8 vec8(std::array<uint32_t, 8> v) noexcept {
    return simd::u32x8([&](auto lane) noexcept -> uint32_t { return v[decltype(lane)::value]; });
}

// The lane index is taken as a size for parity with the arrays used
// throughout the file, and narrowed at the subscript, which wants a
// signed index.
[[nodiscard]] uint32_t at(simd::u32x8 v, std::size_t lane) noexcept { return v[static_cast<int>(lane)]; }

// Runs the scalar generator once per lane and compares the batch
// result against it, lane by lane.
void check_equivalence(std::array<Philox::Ctr, 8> counters, std::array<Philox::Key, 8> keys,
                       const char* what) noexcept {
    // The scalar inputs are per lane; the batch wants one vector per
    // word, so the arrays are transposed on the way in.
    auto ctr0 = vec8({counters[0][0], counters[1][0], counters[2][0], counters[3][0], counters[4][0], counters[5][0],
                      counters[6][0], counters[7][0]});
    auto ctr1 = vec8({counters[0][1], counters[1][1], counters[2][1], counters[3][1], counters[4][1], counters[5][1],
                      counters[6][1], counters[7][1]});
    auto ctr2 = vec8({counters[0][2], counters[1][2], counters[2][2], counters[3][2], counters[4][2], counters[5][2],
                      counters[6][2], counters[7][2]});
    auto ctr3 = vec8({counters[0][3], counters[1][3], counters[2][3], counters[3][3], counters[4][3], counters[5][3],
                      counters[6][3], counters[7][3]});
    auto key0 = vec8({keys[0][0], keys[1][0], keys[2][0], keys[3][0], keys[4][0], keys[5][0], keys[6][0], keys[7][0]});
    auto key1 = vec8({keys[0][1], keys[1][1], keys[2][1], keys[3][1], keys[4][1], keys[5][1], keys[6][1], keys[7][1]});

    auto batch = philox_batch8(ctr0, ctr1, ctr2, ctr3, key0, key1);

    for (std::size_t lane = 0; lane < 8; ++lane) {
        auto expected = Philox::generate(counters[lane], keys[lane]);
        uint32_t got0 = at(batch.r0, lane);
        uint32_t got1 = at(batch.r1, lane);
        uint32_t got2 = at(batch.r2, lane);
        uint32_t got3 = at(batch.r3, lane);

        if (got0 != expected[0] || got1 != expected[1] || got2 != expected[2] || got3 != expected[3]) {
            std::fprintf(stderr,
                         "[%s] lane %zu MISMATCH:\n"
                         "  expected: %08x %08x %08x %08x\n"
                         "       got: %08x %08x %08x %08x\n",
                         what, lane, expected[0], expected[1], expected[2], expected[3], got0, got1, got2, got3);
            std::abort();
        }
    }
}

void test_all_zeros() {
    std::array<Philox::Ctr, 8> counters{};  // value-initialized to zero
    std::array<Philox::Key, 8> keys{};
    check_equivalence(counters, keys, "all-zeros");
    std::printf("  test_all_zeros: PASSED\n");
}

void test_all_ones() {
    std::array<Philox::Ctr, 8> counters;
    std::array<Philox::Key, 8> keys;
    for (std::size_t i = 0; i < 8; ++i) {
        counters[i] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        keys[i] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    }
    check_equivalence(counters, keys, "all-ones");
    std::printf("  test_all_ones: PASSED\n");
}

void test_streaming_counters() {
    // One key broadcast across lanes with the counter advancing per
    // lane is the shape a per-element draw over a row of a tensor
    // takes.
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
    // A distinct key per lane is the shape a batch of unrelated draws
    // takes.  The keys differ in their high half as well as their
    // low, so a lane that mixed only the low half would show up here.
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
    // Values at the extremes of the word.  The widening multiply and
    // shift is where a lane would go wrong, and it goes wrong nearest
    // the wraparound.
    std::array<Philox::Ctr, 8> counters{
        Philox::Ctr{0u, 0u, 0u, 0u},
        Philox::Ctr{0xFFFFFFFFu, 0u, 0u, 0u},
        Philox::Ctr{0x80000000u, 0x80000000u, 0u, 0u},
        Philox::Ctr{0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu},
        Philox::Ctr{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u},
        Philox::Ctr{1u, 0xFFFFFFFEu, 1u, 0xFFFFFFFEu},
        Philox::Ctr{Philox::M0, Philox::M1, Philox::W0, Philox::W1},  // the round constants as input
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
    // Changing one lane's input must leave every other lane's output
    // untouched.  Without that the batch is not eight independent
    // draws at all.
    //
    // Two batches are run whose inputs differ in the first lane
    // alone.  The other lanes carry distinct non-zero inputs, so a
    // leak between lanes would move them.
    std::array<Philox::Ctr, 8> counters_a;
    std::array<Philox::Ctr, 8> counters_b;
    std::array<Philox::Key, 8> keys{};

    counters_a[0] = {0, 0, 0, 0};
    counters_b[0] = {1, 0, 0, 0};  // ← only difference
    keys[0] = {0, 0};

    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters_a[i] = {iu * 100u, iu * 200u, iu * 300u, iu * 400u};
        counters_b[i] = counters_a[i];  // ← identical for lanes 1-7
        keys[i] = {iu * 11u, iu * 13u};
    }

    auto build_v = [&](std::array<Philox::Ctr, 8>& c, std::size_t word) {
        return vec8({c[0][word], c[1][word], c[2][word], c[3][word], c[4][word], c[5][word], c[6][word], c[7][word]});
    };
    auto build_k = [&](std::array<Philox::Key, 8>& k, std::size_t word) {
        return vec8({k[0][word], k[1][word], k[2][word], k[3][word], k[4][word], k[5][word], k[6][word], k[7][word]});
    };

    auto a = philox_batch8(build_v(counters_a, 0), build_v(counters_a, 1), build_v(counters_a, 2),
                           build_v(counters_a, 3), build_k(keys, 0), build_k(keys, 1));
    auto b = philox_batch8(build_v(counters_b, 0), build_v(counters_b, 1), build_v(counters_b, 2),
                           build_v(counters_b, 3), build_k(keys, 0), build_k(keys, 1));

    // Equal outputs from different inputs are possible in principle
    // and vanishingly unlikely here.
    assert(at(a.r0, 0) != at(b.r0, 0) && "lane 0 must change when its input changes");

    for (std::size_t lane = 1; lane < 8; ++lane) {
        assert(at(a.r0, lane) == at(b.r0, lane) && "non-perturbed lane must not change");
        assert(at(a.r1, lane) == at(b.r1, lane));
        assert(at(a.r2, lane) == at(b.r2, lane));
        assert(at(a.r3, lane) == at(b.r3, lane));
    }

    std::printf("  test_per_lane_independence: PASSED\n");
}

void test_known_vector_lane0() {
    // The four words asserted at the end are the generator's
    // published reference vector for an all-zero counter and key.
    // They are fixed outside this project, so they anchor the whole
    // implementation rather than only its internal consistency.
    std::array<Philox::Ctr, 8> counters{};
    std::array<Philox::Key, 8> keys{};

    // The remaining lanes carry noise, which must not reach the first.
    for (std::size_t i = 1; i < 8; ++i) {
        const uint32_t iu = static_cast<uint32_t>(i);
        counters[i] = {0xDEADBEEFu * iu, 0xCAFEBABEu * iu, 0xFEEDFACEu * iu, 0xBADC0DEDu * iu};
        keys[i] = {0xA5A5A5A5u * iu, 0x5A5A5A5Au * iu};
    }

    auto ctr0 = vec8({counters[0][0], counters[1][0], counters[2][0], counters[3][0], counters[4][0], counters[5][0],
                      counters[6][0], counters[7][0]});
    auto ctr1 = vec8({counters[0][1], counters[1][1], counters[2][1], counters[3][1], counters[4][1], counters[5][1],
                      counters[6][1], counters[7][1]});
    auto ctr2 = vec8({counters[0][2], counters[1][2], counters[2][2], counters[3][2], counters[4][2], counters[5][2],
                      counters[6][2], counters[7][2]});
    auto ctr3 = vec8({counters[0][3], counters[1][3], counters[2][3], counters[3][3], counters[4][3], counters[5][3],
                      counters[6][3], counters[7][3]});
    auto key0 = vec8({keys[0][0], keys[1][0], keys[2][0], keys[3][0], keys[4][0], keys[5][0], keys[6][0], keys[7][0]});
    auto key1 = vec8({keys[0][1], keys[1][1], keys[2][1], keys[3][1], keys[4][1], keys[5][1], keys[6][1], keys[7][1]});

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
