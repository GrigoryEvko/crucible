// SchemaTable::lookup — sorted binary-search op-name resolution.
//
// Not on the bg drain hot path; diagnostic / visualization only. Still
// measured for parity with CKernelTable, because the two tables keep their
// entries in the order of the hashes.

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
    // The view asks for the context of a Vigil's producer claim.  The bench
    // uses no Vigil, so it takes that context from the test door.
    const auto mutable_view = table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>())
                                  .expect("a fresh table mints a mutable view");
    SchemaHash hashes[N];
    for (uint32_t i = 0; i < N; i++) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i + 1);
        hashes[i] = SchemaHash{h};
        ::fixy::FixedText<16> name{};
        const ::fixy::TextView name_view =
            ::fixy::format(name, "aten::op{}", i).expect("aten::op and a number below 250 fit in 16 characters");
        const bool was_registered = table.register_name(
            mutable_view, hashes[i], ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(name_view));
        CRUCIBLE_FATAL_INVARIANT(was_registered);
    }

    std::printf("=== schema_table ===\n");
    std::printf("  table entries: %u (log2 = ~8 probes)\n\n", N);

    bench::Report reports[] = {
        [&] {
            uint32_t idx = 0;
            return bench::run("lookup hit  (250-entry table)", [&] {
                const auto h = hashes[idx];
                idx = (idx + 1) % N;
                const bool is_found = table.lookup(h).is_some();
                bench::do_not_optimize(is_found);
            });
        }(),
        [&] {
            uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;
            return bench::run("lookup miss (returns no name)", [&] {
                miss_key = miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
                const bool is_found = table.lookup(SchemaHash{miss_key}).is_some();
                bench::do_not_optimize(is_found);
            });
        }(),
        [&] {
            const auto hot = hashes[N / 2];
            return bench::run("short_name(strip aten::)", [&] {
                const bool is_found = table.short_name(hot).is_some();
                bench::do_not_optimize(is_found);
            });
        }(),
    };

    bench::emit_reports(reports, json);
    return 0;
}
