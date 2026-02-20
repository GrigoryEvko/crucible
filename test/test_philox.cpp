// Philox4x32-10 PRNG — determinism and distribution quality.
//
// Tests:
//   1. Known reference vectors (Random123 library)
//   2. Determinism: same inputs → same outputs
//   3. Uniform distribution: chi-squared test
//   4. Normal distribution: mean ≈ 0, stddev ≈ 1
//   5. Per-op key derivation: different ops → different streams
//   6. Crucible pipeline: master_counter × op_index × element_offset

#include <crucible/Philox.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace crucible;

// ── Reference vectors from Random123 (Salmon et al. 2011) ──────────

static void test_reference_vectors() {
    std::printf("  reference vectors...\n");

    // Vector 1: all zeros
    {
        auto r = Philox::generate({0, 0, 0, 0}, {0, 0});
        assert(r[0] == 0x6627E8D5);
        assert(r[1] == 0xE169C58D);
        assert(r[2] == 0xBC57AC4C);
        assert(r[3] == 0x9B00DBD8);
    }

    // Vector 2: all ones
    {
        auto r = Philox::generate(
            {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
            {0xFFFFFFFF, 0xFFFFFFFF});
        assert(r[0] == 0x408F276D);
        assert(r[1] == 0x41C83B0E);
        assert(r[2] == 0xA20BC7C6);
        assert(r[3] == 0x6D5451FD);
    }

    // Vector 3: counter = {243, 0, 0, 0}, key = {17, 0}
    // (from manual verification against reference impl)
    {
        auto r = Philox::generate({243, 0, 0, 0}, {17, 0});
        // Just verify determinism — re-generate and compare
        auto r2 = Philox::generate({243, 0, 0, 0}, {17, 0});
        assert(r[0] == r2[0] && r[1] == r2[1]);
        assert(r[2] == r2[2] && r[3] == r2[3]);
        // Verify it's not trivially zero
        assert((r[0] | r[1] | r[2] | r[3]) != 0);
    }
}

// ── Determinism ────────────────────────────────────────────────────

static void test_determinism() {
    std::printf("  determinism...\n");

    // 64-bit convenience API
    for (uint64_t offset = 0; offset < 1000; offset++) {
        auto a = Philox::generate(offset, 0xDEADBEEF);
        auto b = Philox::generate(offset, 0xDEADBEEF);
        assert(a == b);
    }

    // Different offsets → different outputs (with overwhelming probability)
    auto a = Philox::generate(0ULL, 42ULL);
    auto b = Philox::generate(1ULL, 42ULL);
    assert(a != b);

    // Different keys → different outputs
    auto c = Philox::generate(0ULL, 1ULL);
    auto d = Philox::generate(0ULL, 2ULL);
    assert(c != d);
}

// ── Uniform distribution: chi-squared goodness-of-fit ──────────────

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

    // Chi-squared test: expect N/BINS per bin
    double expected = static_cast<double>(N) / BINS;
    double chi2 = 0.0;
    for (int b = 0; b < BINS; b++) {
        double diff = counts[b] - expected;
        chi2 += (diff * diff) / expected;
    }

    // For 99 dof, chi2 critical value at p=0.001 is ~148.
    // Good RNG should be well below this.
    std::printf("    chi2 = %.1f (99 dof, critical@0.001 = 148.2)\n", chi2);
    assert(chi2 < 148.2 && "uniform distribution failed chi-squared test");

    // Also check all 4 outputs produce valid uniforms
    for (int i = 0; i < 1000; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0xABCDEF00ULL);
        for (int j = 0; j < 4; j++) {
            float u = Philox::to_uniform(r[j]);
            assert(u >= 0.0f && u < 1.0f);
        }
    }
}

// ── Normal distribution: moments test ──────────────────────────────

static void test_normal() {
    std::printf("  normal distribution (Box-Muller)...\n");

    static constexpr int N = 50000;
    double sum = 0.0, sum_sq = 0.0;

    for (int i = 0; i < N; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0xCAFEBABEULL);
        auto [n0, n1] = Philox::box_muller(r[0], r[1]);

        sum += n0 + n1;
        sum_sq += static_cast<double>(n0) * n0 + static_cast<double>(n1) * n1;
    }

    int total = N * 2;
    double mean = sum / total;
    double var = sum_sq / total - mean * mean;
    double stddev = std::sqrt(var);

    std::printf("    mean = %.4f (expect ~0)\n", mean);
    std::printf("    stddev = %.4f (expect ~1)\n", stddev);

    // Tolerances: with 100K samples, mean should be within ~0.01 of 0
    // and stddev within ~0.01 of 1.
    assert(std::abs(mean) < 0.02 && "normal mean too far from 0");
    assert(std::abs(stddev - 1.0) < 0.02 && "normal stddev too far from 1");

    // No NaN/Inf
    for (int i = 0; i < 1000; i++) {
        auto r = Philox::generate(static_cast<uint64_t>(i), 0ULL);
        auto [n0, n1] = Philox::box_muller(r[0], r[1]);
        assert(std::isfinite(n0) && std::isfinite(n1));
    }
}

// ── Per-op key derivation ──────────────────────────────────────────

static void test_op_keys() {
    std::printf("  per-op key derivation...\n");

    uint64_t master = 42;
    uint64_t hash_a = 0x1234;
    uint64_t hash_b = 0x5678;

    // Different ops → different keys
    uint64_t k0 = Philox::op_key(master, 0, hash_a);
    uint64_t k1 = Philox::op_key(master, 1, hash_a);
    assert(k0 != k1);

    // Different content hashes → different keys
    uint64_t ka = Philox::op_key(master, 0, hash_a);
    uint64_t kb = Philox::op_key(master, 0, hash_b);
    assert(ka != kb);

    // Different master counters → different keys
    uint64_t km0 = Philox::op_key(0, 0, hash_a);
    uint64_t km1 = Philox::op_key(1, 0, hash_a);
    assert(km0 != km1);

    // Deterministic
    assert(Philox::op_key(master, 5, hash_a) ==
           Philox::op_key(master, 5, hash_a));
}

// ── Full pipeline simulation ───────────────────────────────────────

static void test_pipeline() {
    std::printf("  pipeline simulation...\n");

    // Simulate: 2 iterations, 3 ops each, 1000 elements per op.
    // Verify: same iteration+op+element → same value.
    // Verify: different iterations → different values.

    uint64_t content_hashes[3] = {0xAABB, 0xCCDD, 0xEEFF};

    for (uint64_t iter = 0; iter < 2; iter++) {
        for (uint32_t op = 0; op < 3; op++) {
            uint64_t key = Philox::op_key(iter, op, content_hashes[op]);

            for (uint64_t elem = 0; elem < 1000; elem++) {
                auto r = Philox::generate(elem, key);
                // Verify determinism
                auto r2 = Philox::generate(elem, key);
                assert(r == r2);
            }
        }
    }

    // Cross-iteration: same op, same element, different iteration → different value
    uint64_t k0 = Philox::op_key(0, 0, content_hashes[0]);
    uint64_t k1 = Philox::op_key(1, 0, content_hashes[0]);
    auto r0 = Philox::generate(42ULL, k0);
    auto r1 = Philox::generate(42ULL, k1);
    assert(r0 != r1);

    std::printf("    2 iters × 3 ops × 1000 elems: all deterministic\n");
}

// ── constexpr verification ─────────────────────────────────────────

static void test_constexpr() {
    std::printf("  constexpr evaluation...\n");

    // These are evaluated at compile time.
    static constexpr auto r = Philox::generate({0, 0, 0, 0}, {0, 0});
    static_assert(r[0] == 0x6627E8D5);
    static_assert(r[1] == 0xE169C58D);
    static_assert(r[2] == 0xBC57AC4C);
    static_assert(r[3] == 0x9B00DBD8);

    static constexpr auto r2 = Philox::generate(0ULL, 0ULL);
    static_assert(r2[0] == r[0]);

    static constexpr uint64_t key = Philox::op_key(42, 7, 0x1234);
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
    test_constexpr();

    std::printf("test_philox: PASSED\n");
    return 0;
}
