// SchemaTable::lookup — sorted binary search for op-name resolution.
//
// Called by visualization and diagnostic paths (not the hot bg drain
// loop), but the pattern — sorted array + std::ranges::sort at insert
// — is worth measuring alongside bench_ckernel for parity.

#include <crucible/SchemaTable.h>

#include <cstdint>
#include <cstdio>

#include "bench_harness.h"

using crucible::SchemaTable;
using crucible::SchemaHash;

int main() {
    std::printf("bench_schema_table:\n");

    SchemaTable table{};
    constexpr uint32_t N = 250;
    SchemaHash hashes[N];
    char names[N][16];
    for (uint32_t i = 0; i < N; i++) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i + 1);
        hashes[i] = SchemaHash{h};
        std::snprintf(names[i], sizeof(names[i]), "aten::op%u", i);
        table.register_name(hashes[i], names[i]);
    }

    volatile uint32_t idx = 0;
    BENCH("  lookup hit  (250-entry table)", 10'000'000, {
        const auto h = hashes[idx];
        idx = (idx + 1) % N;
        const char* n = table.lookup(h);
        bench::DoNotOptimize(n);
    });

    volatile uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
    BENCH("  lookup miss (returns nullptr)", 10'000'000, {
        miss_key = miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
        const char* n = table.lookup(SchemaHash{miss_key});
        bench::DoNotOptimize(n);
    });

    const auto hot = hashes[N / 2];
    BENCH("  short_name(strip aten::)", 10'000'000, {
        const char* n = table.short_name(hot);
        bench::DoNotOptimize(n);
    });

    std::printf("\nbench_schema_table: %u entries, ~log2 = 8 probes\n", N);
    return 0;
}
