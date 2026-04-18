// Philox4x32-10 throughput bench.
//
// One generate() call produces 4 x uint32 = 16 bytes of random data.
// Target: ≤20 ns / call → ≥800 MB/s on a single thread.  Scales
// linearly across element indices (counter-based, no sequential state).

#include <crucible/Philox.h>

#include <cstdint>
#include <cstdio>

#include "bench_harness.h"

using crucible::Philox;

int main() {
    std::printf("bench_philox:\n");

    volatile uint64_t offset = 0x1234'5678'9abc'def0ULL;
    volatile uint64_t key    = 0xcafe'babe'dead'beefULL;

    BENCH("  generate(offset,key)  [4xu32 = 16B]", 10'000'000, {
        auto ctr = Philox::generate(offset, key);
        bench::DoNotOptimize(ctr);
    });

    auto make_ctr = [](uint32_t a) {
        return Philox::Ctr{a, a + 1, a + 2, a + 3};
    };
    auto make_key = [](uint32_t a) {
        return Philox::Key{a, a + 1};
    };
    volatile uint32_t seed = 1;
    BENCH("  generate raw 4x32 Ctr+Key", 10'000'000, {
        auto r = Philox::generate(make_ctr(seed), make_key(seed + 4));
        bench::DoNotOptimize(r);
    });

    // Element-indexed: mimic per-thread invocation in a parallel kernel.
    // Counter increments per index; key is per-op.
    BENCH("  generate streaming (i, key)", 10'000'000, {
        static uint64_t i = 0;
        auto ctr = Philox::generate(i++, key);
        bench::DoNotOptimize(ctr);
    });

    // Conversions: check to_uniform and box_muller overhead.
    volatile uint32_t raw = 0x12345678;
    BENCH("  to_uniform(u32) → float", 10'000'000, {
        float f = Philox::to_uniform(raw);
        bench::DoNotOptimize(f);
    });

    volatile uint32_t u1 = 0xAABBCCDD;
    volatile uint32_t u2 = 0x11223344;
    BENCH("  box_muller(u32, u32) → 2xf32", 10'000'000, {
        auto pair = Philox::box_muller(u1, u2);
        bench::DoNotOptimize(pair);
    });

    // Per-op key derivation — FNV-1a mix of 3 × 64-bit words.
    volatile uint64_t master = 42;
    volatile uint32_t op_idx = 7;
    crucible::ContentHash content_hash{0xfeedfacecafeULL};
    BENCH("  op_key(master, op_idx, hash)", 10'000'000, {
        uint64_t k = Philox::op_key(master, op_idx, content_hash);
        bench::DoNotOptimize(k);
    });

    // Determinism sanity check — same inputs, same bits.
    auto r1 = Philox::generate(uint64_t{1}, uint64_t{2});
    auto r2 = Philox::generate(uint64_t{1}, uint64_t{2});
    for (int i = 0; i < 4; ++i) {
        if (r1[i] != r2[i]) {
            std::fprintf(stderr, "philox non-deterministic!\n");
            return 1;
        }
    }
    std::printf("\nbench_philox: deterministic, ok\n");
    return 0;
}
