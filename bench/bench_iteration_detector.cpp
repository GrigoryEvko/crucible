// IterationDetector::check() — the hot path called once per drained op
// on the background thread.
//
// Target: ≤1.2 ns/op in steady state (no boundary). Algorithm is a
// sequential match with a cached expected value: ONE compare + ONE
// increment + ONE jne on the hot path. No ring, no fingerprint, no
// multiply.
//
// Six Reports: steady-state, boundary-every-100, cold-start build,
// match-advance, reset, and a 1000-op tight loop. Each scenario gets
// a freshly warmed detector so the bench observes the right phase.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <crucible/IterationDetector.h>

#include "bench_harness.h"

using namespace crucible;

namespace {

// Splitmix64 — fast non-cryptographic PRNG for test hashes. Single
// global state is fine; we read from the pre-generated array, and the
// few warmup calls that mutate it leave the bench reproducible as long
// as warmup order is deterministic (it is).
uint64_t splitmix_state = 0xDEADBEEF42ULL;

uint64_t splitmix64() noexcept {
    uint64_t z = (splitmix_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Pre-generated pool of random hashes. Reading from an array (vs
// splitmix64() inline) keeps PRNG cost out of the measurement.
constexpr uint32_t NUM_HASHES = 1u << 16;   // 65536
SchemaHash random_hashes[NUM_HASHES];

void init_random_hashes() noexcept {
    for (uint32_t i = 0; i < NUM_HASHES; i++) {
        random_hashes[i] = SchemaHash{splitmix64()};
    }
}

// Known pattern for signature-match phase tests.
constexpr SchemaHash SIG[IterationDetector::K] = {
    SchemaHash{0xAAAA'BBBB'CCCC'0001ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0002ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0003ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0004ULL},
    SchemaHash{0xAAAA'BBBB'CCCC'0005ULL},
};

// Warm the detector past signature-build + candidate phases → confirmed
// steady state.  Two full iterations (signature + body × 2) followed by
// a 3rd K-match so the final boundary has just fired.
void warmup_detector(IterationDetector& d) {
    d.reset();
    for (uint32_t i = 0; i < IterationDetector::K; i++) (void)d.check(SIG[i]);
    for (uint32_t i = 0; i < 100; i++) (void)d.check(SchemaHash{splitmix64()});
    for (uint32_t i = 0; i < IterationDetector::K; i++) (void)d.check(SIG[i]);
    for (uint32_t i = 0; i < 100; i++) (void)d.check(SchemaHash{splitmix64()});
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        bool boundary = d.check(SIG[i]);
        if (i == IterationDetector::K - 1 && !boundary) {
            std::fprintf(stderr, "ERROR: warmup did not produce a boundary\n");
            std::abort();
        }
    }
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== iteration_detector ===\n");
    std::printf("  sizeof(IterationDetector): %zu bytes\n\n",
                sizeof(IterationDetector));

    init_random_hashes();

    bench::Report reports[] = {
        // 1. Steady state — no boundary, ~99.5% of real calls. Hot path
        // is one cmp + one inc, everything in the same cache line.
        [&]{
            IterationDetector d;
            warmup_detector(d);
            uint32_t hash_idx = 0;
            return bench::run("check() steady state (no match)", [&]{
                bool b = d.check(random_hashes[hash_idx & (NUM_HASHES - 1)]);
                bench::do_not_optimize(b);
                hash_idx++;
            });
        }(),

        // 2. Periodic boundaries — 95 random + 5 signature per 100-op
        // cycle. Amortized cost including boundary handling.
        [&]{
            IterationDetector d;
            warmup_detector(d);
            constexpr uint32_t ITER_LEN = 100;
            SchemaHash pattern[ITER_LEN];
            for (uint32_t i = 0; i < ITER_LEN - IterationDetector::K; i++) {
                pattern[i] = SchemaHash{splitmix64()};
            }
            for (uint32_t i = 0; i < IterationDetector::K; i++) {
                pattern[ITER_LEN - IterationDetector::K + i] = SIG[i];
            }
            uint32_t pat_idx = 0;
            return bench::run("check() boundaries / 100 ops", [&]{
                bool b = d.check(pattern[pat_idx]);
                bench::do_not_optimize(b);
                if (++pat_idx >= ITER_LEN) pat_idx = 0;
            });
        }(),

        // 3. Signature build from a fresh detector — exercises the
        // cold-start path (signature_len < K, K compares + K stores).
        [&]{
            IterationDetector d;
            return bench::run("check() signature build (K=5)", [&]{
                d.reset();
                for (uint32_t i = 0; i < IterationDetector::K; i++) {
                    bool b = d.check(SIG[i]);
                    bench::do_not_optimize(b);
                }
            });
        }(),

        // 4. Match advance — K consecutive hits + 10 randoms to fall
        // back to match_pos_=0. Isolates the match-advance hot path
        // (increment match_pos_, write expected_hash_).
        [&]{
            IterationDetector d;
            warmup_detector(d);
            return bench::run("check() match advance (K=5 + 10 rand)", [&]{
                for (uint32_t i = 0; i < IterationDetector::K; i++) {
                    bool b = d.check(SIG[i]);
                    bench::do_not_optimize(b);
                }
                for (uint32_t i = 0; i < 10; i++) {
                    (void)d.check(SchemaHash{splitmix64()});
                }
            });
        }(),

        // 5. reset() — rare (only when current_trace is recycled), but
        // worth a known upper bound. do_not_optimize on signature_len
        // implies a "memory" clobber, so no additional fence needed.
        [&]{
            IterationDetector d;
            warmup_detector(d);
            return bench::run("reset()", [&]{
                d.reset();
                bench::do_not_optimize(d.signature_len);
            });
        }(),

        // 6. Sustained 1000-op batch — simulates one bg-drain cycle
        // through a full kernel epoch. p50 divided by 1000 = effective
        // per-op cost at scale.
        [&]{
            IterationDetector d;
            warmup_detector(d);
            constexpr uint32_t BATCH = 1000;
            uint32_t hash_idx = 0;
            return bench::run("check() × 1000 (sustained)", [&]{
                for (uint32_t i = 0; i < BATCH; i++) {
                    bool b = d.check(random_hashes[hash_idx & (NUM_HASHES - 1)]);
                    bench::do_not_optimize(b);
                    hash_idx++;
                }
            });
        }(),
    };

    bench::emit_reports(reports, json);
    return 0;
}
