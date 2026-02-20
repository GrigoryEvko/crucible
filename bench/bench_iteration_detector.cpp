// Benchmark for IterationDetector::check() -- the hot path called once per
// drained op on the background thread.
//
// Target: <=1.2ns/op in steady state (no boundary detected).
//
// Algorithm: sequential matching with cached expected value.
// Hot path is ONE comparison (expected_hash_ vs incoming) + ONE increment.
// No fingerprint, no ring buffer, no multiply.

#include "bench_harness.h"

#include <crucible/IterationDetector.h>

#include <cstdio>
#include <cstdlib>

using namespace crucible;

// Splitmix64 -- fast, non-cryptographic PRNG for generating test hashes.
static uint64_t splitmix_state = 0xDEADBEEF42ULL;

static uint64_t splitmix64() {
    uint64_t z = (splitmix_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Pre-generate N random hashes to avoid PRNG cost in the measurement loop.
static constexpr uint32_t NUM_HASHES = 1 << 16; // 65536
static SchemaHash random_hashes[NUM_HASHES];

static void init_random_hashes() {
    for (uint32_t i = 0; i < NUM_HASHES; i++) {
        random_hashes[i] = SchemaHash{splitmix64()};
    }
}

// Fixed signature for benchmarks that need a known pattern.
static constexpr SchemaHash SIG[IterationDetector::K] = {
    SchemaHash{0xAAAA'BBBB'CCCC'0001ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0002ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0003ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0004ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0005ULL},
};

// Warm up detector past the signature-build and candidate phases.
// After this, the detector is in confirmed steady state.
static void warmup_detector(IterationDetector& d) {
    d.reset();
    // Iteration 0: build signature (K ops).
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        (void)d.check(SIG[i]);
    }
    // Iteration 0 body: 100 random ops.
    for (uint32_t i = 0; i < 100; i++) {
        (void)d.check(SchemaHash{splitmix64()});
    }
    // Iteration 1 trigger: candidate match (K ops).
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        (void)d.check(SIG[i]);
    }
    // Iteration 1 body: 100 random ops.
    for (uint32_t i = 0; i < 100; i++) {
        (void)d.check(SchemaHash{splitmix64()});
    }
    // Iteration 2 trigger: confirmed match (K ops).
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        bool boundary = d.check(SIG[i]);
        // The K-th op of the 3rd repetition should fire.
        if (i == IterationDetector::K - 1 && !boundary) {
            std::fprintf(stderr, "ERROR: warmup did not produce a boundary\n");
            std::abort();
        }
    }
}

int main() {
    std::printf("=== IterationDetector Benchmark ===\n");
    std::printf("    sizeof(IterationDetector) = %zu\n\n", sizeof(IterationDetector));
    init_random_hashes();

    // ────────────────────────────────────────────────────────
    // 1. Steady state: no boundary, random hashes.
    //    This is the dominant case (99.5% of calls).
    //    Hot path: inc + cmp + jne. No writes beyond increment.
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        warmup_detector(d);

        uint32_t hash_idx = 0;
        BENCH_ROUNDS("check() steady state (no match)", 10'000'000, 21, {
            bool b = d.check(random_hashes[hash_idx & (NUM_HASHES - 1)]);
            bench::DoNotOptimize(b);
            hash_idx++;
        });
    }

    // ────────────────────────────────────────────────────────
    // 2. Periodic boundaries: signature match every 100 ops.
    //    Measures amortized cost including boundary handling.
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        warmup_detector(d);

        // Pre-build a pattern: 95 random + 5 signature = 100 ops/boundary.
        static constexpr uint32_t ITER_LEN = 100;
        static constexpr uint32_t TOTAL = ITER_LEN * 1000; // 1000 boundaries
        SchemaHash pattern[ITER_LEN];
        for (uint32_t i = 0; i < ITER_LEN - IterationDetector::K; i++) {
            pattern[i] = SchemaHash{splitmix64()};
        }
        for (uint32_t i = 0; i < IterationDetector::K; i++) {
            pattern[ITER_LEN - IterationDetector::K + i] = SIG[i];
        }

        uint32_t pat_idx = 0;
        uint32_t boundary_count = 0;
        BENCH_ROUNDS("check() with boundaries every 100 ops", TOTAL, 11, {
            bool b = d.check(pattern[pat_idx]);
            if (b) boundary_count++;
            bench::DoNotOptimize(b);
            if (++pat_idx >= ITER_LEN) pat_idx = 0;
        });
        (void)boundary_count;
    }

    // ────────────────────────────────────────────────────────
    // 3. Signature build: feeding first K ops (cold start).
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        BENCH_ROUNDS("check() signature build (K=5 cold start)", 1'000'000, 11, {
            d.reset();
            for (uint32_t i = 0; i < IterationDetector::K; i++) {
                bool b = d.check(SIG[i]);
                bench::DoNotOptimize(b);
            }
        });
    }

    // ────────────────────────────────────────────────────────
    // 4. Match advance: sequential matching through K=5 hits.
    //    Isolates the match path (increment + write expected_hash_).
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        warmup_detector(d);

        // Feed exactly the signature K times, measuring the match-advance path.
        BENCH_ROUNDS("check() match advance (K=5 consecutive)", 1'000'000, 11, {
            for (uint32_t i = 0; i < IterationDetector::K; i++) {
                bool b = d.check(SIG[i]);
                bench::DoNotOptimize(b);
            }
            // Feed some random ops to get back to match_pos_=0
            for (uint32_t i = 0; i < 10; i++) {
                (void)d.check(SchemaHash{splitmix64()});
            }
        });
    }

    // ────────────────────────────────────────────────────────
    // 5. Reset cost.
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        warmup_detector(d);
        BENCH_ROUNDS("reset()", 1'000'000, 11, {
            d.reset();
            bench::DoNotOptimize(d.signature_len);
            bench::ClobberMemory();
        });
    }

    // ────────────────────────────────────────────────────────
    // 6. Tight loop: simulate background thread draining 1000 ops.
    //    Measures sustained throughput with realistic access patterns.
    // ────────────────────────────────────────────────────────
    {
        IterationDetector d;
        warmup_detector(d);

        static constexpr uint32_t BATCH = 1000;
        uint32_t hash_idx = 0;
        BENCH_ROUNDS("check() x1000 batch (sustained)", BATCH, 11, {
            for (uint32_t i = 0; i < BATCH; i++) {
                bool b = d.check(random_hashes[hash_idx & (NUM_HASHES - 1)]);
                bench::DoNotOptimize(b);
                hash_idx++;
            }
        });
    }

    std::printf("\nDone.\n");
    return 0;
}
