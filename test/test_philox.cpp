#include <crucible/Philox.h>

#include <array>
#include "test_assert.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace crucible;

[[nodiscard]] static constexpr uint64_t philox_key(uint64_t master_counter, uint32_t op_index,
                                                   ContentHash content_hash) noexcept {
    return Philox::op_key_det(master_counter, op_index, content_hash).peek();
}

// The expected words in the first two cases are the published
// reference vectors for this generator.  They are not values recorded
// from this implementation, so they fail if the round function drifts.

static void test_reference_vectors() {
    std::printf("  reference vectors...\n");

    {
        auto r = Philox::generate({0, 0, 0, 0}, {0, 0});
        assert(r[0] == 0x6627E8D5);
        assert(r[1] == 0xE169C58D);
        assert(r[2] == 0xBC57AC4C);
        assert(r[3] == 0x9B00DBD8);
    }

    {
        auto r = Philox::generate({0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}, {0xFFFFFFFF, 0xFFFFFFFF});
        assert(r[0] == 0x408F276D);
        assert(r[1] == 0x41C83B0E);
        assert(r[2] == 0xA20BC7C6);
        assert(r[3] == 0x6D5451FD);
    }

    // This input has no published vector, so the only claims available
    // are that the output repeats and that it is not all zero.
    {
        auto r = Philox::generate({243, 0, 0, 0}, {17, 0});
        auto r2 = Philox::generate({243, 0, 0, 0}, {17, 0});
        assert(r[0] == r2[0] && r[1] == r2[1]);
        assert(r[2] == r2[2] && r[3] == r2[3]);
        assert((r[0] | r[1] | r[2] | r[3]) != 0);
    }
}

static void test_determinism() {
    std::printf("  determinism...\n");

    for (uint64_t offset = 0; offset < 1000; offset++) {
        auto a = Philox::generate(offset, 0xDEADBEEF);
        auto b = Philox::generate(offset, 0xDEADBEEF);
        assert(a == b);
    }

    // Two distinct inputs could in principle collide on all four words.
    // The probability is small enough to assert against.
    auto a = Philox::generate(0ULL, 42ULL);
    auto b = Philox::generate(1ULL, 42ULL);
    assert(a != b);

    auto c = Philox::generate(0ULL, 1ULL);
    auto d = Philox::generate(0ULL, 2ULL);
    assert(c != d);
}

static void test_uniform() {
    std::printf("  uniform distribution...\n");

    static constexpr int N = 100000;
    static constexpr int BINS = 100;
    int counts[BINS]{};

    for (int i = 0; i < N; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0x12345678ULL);
        float u = Philox::to_uniform(r[0]);
        assert(u >= 0.0f && u < 1.0f);
        int bin = static_cast<int>(u * BINS);
        if (bin >= BINS) bin = BINS - 1;
        counts[bin]++;
    }

    double expected = static_cast<double>(N) / BINS;
    double chi2 = 0.0;
    for (int b = 0; b < BINS; b++) {
        double diff = counts[b] - expected;
        chi2 += (diff * diff) / expected;
    }

    // The bound is the chi-squared critical value for 99 degrees of
    // freedom at p = 0.001.  A sound generator lands far below it, so a
    // failure here means a real distribution defect rather than an
    // unlucky run.
    std::printf("    chi2 = %.1f (99 dof, critical@0.001 = 148.2)\n", chi2);
    assert(chi2 < 148.2 && "uniform distribution failed chi-squared test");

    // The histogram above only ever reads the first word, so the other
    // three are range-checked separately.
    for (int i = 0; i < 1000; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0xABCDEF00ULL);
        for (int j = 0; j < 4; j++) {
            float u = Philox::to_uniform(r[static_cast<size_t>(j)]);
            assert(u >= 0.0f && u < 1.0f);
        }
    }
}

static void test_normal() {
    std::printf("  normal distribution (Box-Muller)...\n");

    static constexpr int N = 50000;
    double sum = 0.0, sum_sq = 0.0;

    for (int i = 0; i < N; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0xCAFEBABEULL);
        auto [n0, n1] = Philox::box_muller(r[0], r[1]);

        sum += static_cast<double>(n0) + static_cast<double>(n1);
        sum_sq += static_cast<double>(n0) * static_cast<double>(n0) + static_cast<double>(n1) * static_cast<double>(n1);
    }

    int total = N * 2;
    double mean = sum / total;
    double var = sum_sq / total - mean * mean;
    double stddev = std::sqrt(var);

    std::printf("    mean = %.4f (expect ~0)\n", mean);
    std::printf("    stddev = %.4f (expect ~1)\n", stddev);

    // At this sample count the sample mean stays inside about 0.01 of
    // zero and the sample deviation inside about 0.01 of one, so the
    // bound is set at twice that.
    assert(std::abs(mean) < 0.02 && "normal mean too far from 0");
    assert(std::abs(stddev - 1.0) < 0.02 && "normal stddev too far from 1");

    for (int i = 0; i < 1000; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0ULL);
        auto [n0, n1] = Philox::box_muller(r[0], r[1]);
        assert(std::isfinite(n0) && std::isfinite(n1));
    }

    // The transform shifts its first input by one before taking a
    // logarithm, which is what keeps a raw zero from reaching log(0).
    // A random loop essentially never draws that zero, so it cannot
    // catch the shift being dropped.  The four corners are therefore
    // driven by hand: a raw zero is the logarithm edge, and a raw
    // all-ones maps to one, whose logarithm is zero.
    for (uint32_t u1 : {0u, UINT32_MAX}) {
        for (uint32_t u2 : {0u, UINT32_MAX}) {
            auto [c0, c1] = Philox::box_muller(u1, u2);
            assert(std::isfinite(c0) && std::isfinite(c1)
                   && "box_muller boundary corner must be finite (log(0) guard)");
        }
    }
}

// Each of the three inputs to the key derivation is varied on its own,
// so a derivation that ignored one of them would show up as a collision.
static void test_op_keys() {
    std::printf("  per-op key derivation...\n");

    uint64_t master = 42;
    crucible::ContentHash hash_a{0x1234};
    crucible::ContentHash hash_b{0x5678};

    uint64_t k0 = philox_key(master, 0, hash_a);
    uint64_t k1 = philox_key(master, 1, hash_a);
    assert(k0 != k1);

    uint64_t ka = philox_key(master, 0, hash_a);
    uint64_t kb = philox_key(master, 0, hash_b);
    assert(ka != kb);

    uint64_t km0 = philox_key(0, 0, hash_a);
    uint64_t km1 = philox_key(1, 0, hash_a);
    assert(km0 != km1);

    assert(philox_key(master, 5, hash_a) == philox_key(master, 5, hash_a));
}

static void test_pipeline() {
    std::printf("  pipeline simulation...\n");

    crucible::ContentHash content_hashes[3] = {crucible::ContentHash{0xAABB}, crucible::ContentHash{0xCCDD},
                                               crucible::ContentHash{0xEEFF}};

    for (uint64_t iter = 0; iter < 2; iter++) {
        for (uint32_t op = 0; op < 3; op++) {
            uint64_t key = philox_key(iter, op, content_hashes[op]);

            for (uint64_t elem = 0; elem < 1000; elem++) {
                auto r = Philox::generate(elem, key);
                auto r2 = Philox::generate(elem, key);
                assert(r == r2);
            }
        }
    }

    // Same op and same element, one iteration apart: the streams must
    // still separate, or a replay would reuse the previous draw.
    uint64_t k0 = philox_key(0, 0, content_hashes[0]);
    uint64_t k1 = philox_key(1, 0, content_hashes[0]);
    auto r0 = Philox::generate(42ULL, k0);
    auto r1 = Philox::generate(42ULL, k1);
    assert(r0 != r1);

    std::printf("    2 iters × 3 ops × 1000 elems: all deterministic\n");
}

// The standard library ships the same generator with the same
// multipliers and round count, so the block itself matches.  Two
// interface details keep the match from being automatic:
//
//   * Its counter setter reverses the array it is handed, and its
//     output is read back in descending index order, which cancels the
//     reversal.  Feeding the counter pre-reversed therefore lines both
//     ends up.
//   * Its seeding interface can only set the first word of the key.
//     The second word is reachable only through a seed sequence, which
//     runs the inputs through a derivation step, so a key with a
//     non-zero second word cannot be reproduced through that interface
//     at all.
//
// The second point is what makes this local generator non-negotiable:
// the key derivation here produces full 64-bit keys.  Any batched
// version must be checked against this scalar implementation, not
// against the standard one.

static void test_std_philox_equivalence() {
    std::printf("  std::philox4x32 bit-equivalence audit...\n");

    auto run_std = [](uint32_t k0, const std::array<uint32_t, 4>& counter) -> std::array<uint32_t, 4> {
        std::philox4x32 stl;
        stl.seed(k0);
        // The setter reverses what it is given, so the counter goes in
        // backwards to come out in the order used here.
        stl.set_counter({counter[3], counter[2], counter[1], counter[0]});
        return {static_cast<uint32_t>(stl()), static_cast<uint32_t>(stl()), static_cast<uint32_t>(stl()),
                static_cast<uint32_t>(stl())};
    };

    {
        auto cru = Philox::generate({0, 0, 0, 0}, {0, 0});
        auto stl = run_std(0, {0, 0, 0, 0});
        assert(cru == stl && "zero/zero block must match std::philox4x32");
    }

    {
        Philox::Ctr ctr{0xDEADBEEF, 0x12345678, 0xCAFEBABE, 0x87654321};
        auto cru = Philox::generate(ctr, {0xA5A5A5A5, 0});
        auto stl = run_std(0xA5A5A5A5, ctr);
        assert(cru == stl && "k1=0 case must be bit-equivalent");
    }

    // The seed is fixed, so a failing trial is reproducible.
    {
        std::mt19937_64 rng{0xCAFEBEEFD00DULL};
        for (int trial = 0; trial < 1000; ++trial) {
            Philox::Ctr ctr{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                            static_cast<uint32_t>(rng())};
            uint32_t k0 = static_cast<uint32_t>(rng());
            auto cru = Philox::generate(ctr, {k0, 0});
            auto stl = run_std(k0, ctr);
            assert(cru == stl && "random (counter, k0, 0) trial must be bit-equivalent");
        }
    }

    // This assertion is deliberately inverted.  It records the gap, so
    // that a standard library which grows a way to set the second key
    // word turns this red and the substitution question gets asked
    // again rather than being silently missed.
    {
        Philox::Ctr ctr{1, 2, 3, 4};
        auto cru = Philox::generate(ctr, {0xAAAA, 0xBBBB});
        auto stl = run_std(0xAAAA, ctr);
        assert(cru != stl
               && "a non-zero second key word must NOT match a seed of the "
                  "first word alone. If this assertion fails, the standard "
                  "engine has gained a way to set the second key word and "
                  "the substitution question is worth revisiting.");
    }

    std::printf("    block cipher matches; k1=0 fully equivalent; "
                "k1!=0 not reproducible (documented gap)\n");
}

// The same reference vector as above, reached through constant
// evaluation: the generator must fold to identical words at compile
// time and at run time.
static void test_constexpr() {
    std::printf("  constexpr evaluation...\n");

    static constexpr auto r = Philox::generate({0, 0, 0, 0}, {0, 0});
    static_assert(r[0] == 0x6627E8D5);
    static_assert(r[1] == 0xE169C58D);
    static_assert(r[2] == 0xBC57AC4C);
    static_assert(r[3] == 0x9B00DBD8);

    static constexpr auto r2 = Philox::generate(0ULL, 0ULL);
    static_assert(r2[0] == r[0]);

    static constexpr uint64_t key = philox_key(42, 7, ContentHash{0x1234});
    static_assert(key != 0);

    static constexpr float u = Philox::to_uniform(r[0]);
    static_assert(u >= 0.0f && u < 1.0f);
}

int main() {
    std::printf("test_philox:\n");

    test_reference_vectors();
    test_determinism();
    test_uniform();
    test_normal();
    test_op_keys();
    test_pipeline();
    test_std_philox_equivalence();
    test_constexpr();

    std::printf("test_philox: PASSED\n");
    return 0;
}
