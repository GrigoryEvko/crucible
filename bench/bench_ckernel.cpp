// CKernelTable::classify — binary-search dispatch lookup.
//
// Called once per op during build_trace() (bg thread).  At 146 real
// ops + ~50 aliases, the table typically holds ~200 entries: log2(200)
// ≈ 8 comparisons.  Target: ≤20 ns for hit, ≤20 ns for miss (OPAQUE).

#include <crucible/CKernel.h>

#include <cstdint>
#include <cstdio>

#include "bench_harness.h"

using crucible::CKernelTable;
using crucible::CKernelId;
using crucible::SchemaHash;

int main() {
    std::printf("bench_ckernel:\n");

    // Populate a realistic table: 200 entries spread over the hash space.
    CKernelTable table{};
    constexpr uint32_t N = 200;
    SchemaHash hit_hashes[N];
    for (uint32_t i = 0; i < N; i++) {
        // Deterministic but uneven spread.
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i + 1);
        hit_hashes[i] = SchemaHash{h};
        // Pick a CKernelId round-robin; doesn't affect lookup time.
        const auto id = static_cast<CKernelId>(
            1 + (i % (static_cast<uint32_t>(CKernelId::NUM_KERNELS) - 1)));
        table.register_op(hit_hashes[i], id);
    }

    // ── Hit: rotate through pre-registered hashes ─────────────────
    volatile uint32_t idx = 0;
    BENCH("  classify hit  (200-entry table)", 10'000'000, {
        const auto h = hit_hashes[idx];
        idx = (idx + 1) % N;
        const auto id = table.classify(h);
        bench::DoNotOptimize(id);
    });

    // ── Miss: uniformly-random hashes unlikely to hit ─────────────
    volatile uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
    BENCH("  classify miss (OPAQUE fallback)", 10'000'000, {
        miss_key = miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto id = table.classify(SchemaHash{miss_key});
        bench::DoNotOptimize(id);
    });

    // ── Repeated hit on the same hash — branch predictor loves this ──
    const auto hot = hit_hashes[N / 2];
    BENCH("  classify hot  (same hash repeated)", 10'000'000, {
        const auto id = table.classify(hot);
        bench::DoNotOptimize(id);
    });

    std::printf("\nbench_ckernel: %u entries, ~log2 = 8 probes\n", N);
    return 0;
}
