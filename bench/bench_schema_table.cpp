// SchemaTable::lookup — sorted binary-search op-name resolution.
//
// Not on the bg drain hot path; diagnostic / visualization only. Still
// measured for parity with CKernelTable since they share the "sorted
// array + std::ranges::sort on insert" pattern.

#include <cstdint>
#include <cstdio>

#include <crucible/SchemaTable.h>

#include "bench_harness.h"

using crucible::SchemaHash;
using crucible::SchemaTable;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    constexpr uint32_t N = 250;
    SchemaTable table{};
    SchemaHash  hashes[N];
    char        names[N][16];
    for (uint32_t i = 0; i < N; i++) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i + 1);
        hashes[i] = SchemaHash{h};
        std::snprintf(names[i], sizeof(names[i]), "aten::op%u", i);
        table.register_name(hashes[i],
            crucible::SchemaTable::SanitizedName{names[i]});
    }

    std::printf("=== schema_table ===\n");
    std::printf("  table entries: %u (log2 = ~8 probes)\n\n", N);

    bench::Report reports[] = {
        [&]{
            uint32_t idx = 0;
            return bench::run("lookup hit  (250-entry table)", [&]{
                const auto h = hashes[idx];
                idx = (idx + 1) % N;
                const char* n = table.lookup(h);
                bench::do_not_optimize(n);
            });
        }(),
        [&]{
            uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
            return bench::run("lookup miss (returns nullptr)", [&]{
                miss_key = miss_key * 6364136223846793005ULL
                         + 1442695040888963407ULL;
                const char* n = table.lookup(SchemaHash{miss_key});
                bench::do_not_optimize(n);
            });
        }(),
        [&]{
            const auto hot = hashes[N / 2];
            return bench::run("short_name(strip aten::)", [&]{
                const char* n = table.short_name(hot);
                bench::do_not_optimize(n);
            });
        }(),
    };

    bench::emit_reports(reports, json);
    return 0;
}
