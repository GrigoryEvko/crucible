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

#include <cstddef>
#include <cstdint>

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
constexpr uint32_t NUM_HASHES = 1u << 16;  // 65536

// The allocation capability of each detector of the bench.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;
::fixy::FixedArray<SchemaHash, NUM_HASHES> random_hashes{};

void init_random_hashes() noexcept {
    for (SchemaHash& hash : random_hashes) {
        hash = SchemaHash{splitmix64()};
    }
}

// Known pattern for signature-match phase tests.
constexpr ::fixy::FixedArray<SchemaHash, IterationDetector::K> SIG{
    SchemaHash{0xAAAABBBBCCCC0001ULL}, SchemaHash{0xAAAABBBBCCCC0002ULL}, SchemaHash{0xAAAABBBBCCCC0003ULL},
    SchemaHash{0xAAAABBBBCCCC0004ULL}, SchemaHash{0xAAAABBBBCCCC0005ULL},
};

// The iteration of the warmup: the K ops of SIG, then 100 ops that the
// warmup fixes once.
constexpr uint32_t WARMUP_LEN = IterationDetector::K + 100;

// The detector accepts a period after it sees an iteration twice, so this
// many turns give a boundary with room to spare.
constexpr uint32_t WARMUP_MAX_TURNS = 8;

// Warm the detector into a confirmed steady state.  The warmup feeds one
// fixed iteration again and again, and it stops at the first boundary, so
// the boundary has just fired.  The detector accepts a period only when the
// ops of one iteration repeat, so random ops between two copies of SIG give
// no boundary.
void warmup_detector(IterationDetector& d) {
    d.reset();
    ::foundation::core::FixedArray<SchemaHash, WARMUP_LEN> iteration{};
    for (::foundation::core::Index<WARMUP_LEN> const index : iteration.indices()) {
        iteration.at(index) = index.raw() < IterationDetector::K
                                ? SIG.at(::fixy::Index<IterationDetector::K>::check(index.raw())
                                             .expect("the test above keeps the index below K"))
                                : SchemaHash{splitmix64()};
    }
    for (uint32_t turn = 0; turn < WARMUP_MAX_TURNS; turn++) {
        for (SchemaHash const hash : iteration) {
            if (d.check(kAlloc, hash)) return;
        }
    }
    ::fixy::fatal("the warmup of the detector gave no boundary in {} turns", WARMUP_MAX_TURNS);
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    ::fixy::report(::fixy::Sink::Out, "=== iteration_detector ===\n");
    ::fixy::report(::fixy::Sink::Out, "  sizeof(IterationDetector): {} bytes\n\n", sizeof(IterationDetector));

    init_random_hashes();

    ::fixy::FixedArray<bench::Report, 6> reports{
        // 1. Steady state — no boundary, ~99.5% of real calls. Hot path
        // is one cmp + one inc, everything in the same cache line.
        [&] {
            IterationDetector d;
            warmup_detector(d);
            uint32_t hash_idx = 0;
            // The mask keeps the position below NUM_HASHES, so the compiler
            // folds the check of the index.
            return bench::run("check() steady state (no match)", [&] {
                bool b =
                    d.check(kAlloc, random_hashes.at(::fixy::Index<NUM_HASHES>::check(hash_idx & (NUM_HASHES - 1))
                                                         .expect("a mask of the low bits stays below NUM_HASHES")));
                bench::do_not_optimize(b);
                hash_idx++;
            });
        }(),

        // 2. Periodic boundaries — 95 random + 5 signature per 100-op
        // cycle. Amortized cost including boundary handling.
        [&] {
            IterationDetector d;
            warmup_detector(d);
            constexpr uint32_t ITER_LEN = 100;
            constexpr uint32_t kRandomOps = ITER_LEN - IterationDetector::K;
            // The 95 random ops in order, then the K ops of SIG.
            ::fixy::FixedArray<SchemaHash, ITER_LEN> pattern{};
            for (::fixy::Index<ITER_LEN> const i : pattern.indices()) {
                pattern.at(i) = i.raw() < kRandomOps
                                  ? SchemaHash{splitmix64()}
                                  : SIG.at(::fixy::Index<IterationDetector::K>::check(i.raw() - kRandomOps)
                                               .expect("the last K positions take the ops of SIG"));
            }
            // The next position is below ITER_LEN on each arm, so the
            // compiler folds the check of the index.
            ::fixy::Index<ITER_LEN> pat_idx = ::fixy::index<0>();
            return bench::run("check() boundaries / 100 ops", [&] {
                bool b = d.check(kAlloc, pattern.at(pat_idx));
                bench::do_not_optimize(b);
                std::size_t const next = pat_idx.raw() + 1;
                pat_idx = ::fixy::Index<ITER_LEN>::check(next >= ITER_LEN ? 0 : next)
                              .expect("the next position wraps to 0 at ITER_LEN");
            });
        }(),

        // 3. Signature build from a fresh detector — exercises the
        // cold-start path (signature_len < K, K compares + K stores).
        [&] {
            IterationDetector d;
            return bench::run("check() signature build (K=5)", [&] {
                d.reset();
                for (SchemaHash const hash : SIG) {
                    bool b = d.check(kAlloc, hash);
                    bench::do_not_optimize(b);
                }
            });
        }(),

        // 4. Repeated windows: the K ops of an earlier window, then 10
        // random ops.  The window that ends at the last of the K ops has
        // earlier copies, so the search tests candidates on that op.
        [&] {
            IterationDetector d;
            warmup_detector(d);
            return bench::run("check() match advance (K=5 + 10 rand)", [&] {
                for (SchemaHash const hash : SIG) {
                    bool b = d.check(kAlloc, hash);
                    bench::do_not_optimize(b);
                }
                for (uint32_t i = 0; i < 10; i++) {
                    (void)d.check(kAlloc, SchemaHash{splitmix64()});
                }
            });
        }(),

        // 5. reset() — rare (only when current_trace is recycled), but
        // worth a known upper bound. do_not_optimize on signature_len
        // implies a "memory" clobber, so no additional fence needed.
        [&] {
            IterationDetector d;
            warmup_detector(d);
            return bench::run("reset()", [&] {
                d.reset();
                bench::do_not_optimize(d.signature_len.get());
            });
        }(),

        // 6. Sustained 1000-op batch — simulates one bg-drain cycle
        // through a full kernel epoch. p50 divided by 1000 = effective
        // per-op cost at scale.
        [&] {
            IterationDetector d;
            warmup_detector(d);
            constexpr uint32_t BATCH = 1000;
            uint32_t hash_idx = 0;
            return bench::run("check() × 1000 (sustained)", [&] {
                for (uint32_t i = 0; i < BATCH; i++) {
                    bool b =
                        d.check(kAlloc, random_hashes.at(::fixy::Index<NUM_HASHES>::check(hash_idx & (NUM_HASHES - 1))
                                                             .expect("a mask of the low bits stays below NUM_HASHES")));
                    bench::do_not_optimize(b);
                    hash_idx++;
                }
            });
        }(),
    };

    bench::emit_reports(reports.view(), json);
    return 0;
}
