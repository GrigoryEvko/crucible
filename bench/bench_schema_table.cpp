// SchemaTable::lookup — sorted binary-search op-name resolution.
//
// Not on the bg drain hot path; diagnostic / visualization only. Still
// measured for parity with CKernelTable, because the two tables keep their
// entries in the order of the hashes.

#include <cstdint>

#include <crucible/SchemaTable.h>

#include "bench_harness.h"

using crucible::SchemaHash;
using crucible::SchemaTable;

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    constexpr uint32_t N = 250;
    SchemaTable table{};
    // The view and each registration ask for the context of a Vigil's
    // producer claim.  The bench uses no Vigil, so it takes that context from
    // the test door.  The bench has one writer, so the writer of the claim
    // takes the section with one compare-exchange.
    constexpr crucible::VigilFgCtx writer = ::foundation::effects::testing::foreground<crucible::Vigil>();
    const auto mutable_view = table.mint_mutable_view(writer).expect("a fresh table mints a mutable view");
    ::fixy::FixedArray<SchemaHash, N> hashes{};
    for (::fixy::Index<N> const i : hashes.indices()) {
        const uint64_t h = 0x9E3779B97F4A7C15ULL * (i.raw() + 1);
        hashes.with_element(i, [h](SchemaHash& hash) noexcept { hash = SchemaHash{h}; });
        ::fixy::FixedText<16> name{};
        const ::fixy::TextView name_view =
            ::fixy::format(name, "aten::op{}", i.raw()).expect("aten::op and a number below 250 fit in 16 characters");
        const bool was_registered =
            table
                .register_name(writer, ::foundation::effects::testing::test().alloc, mutable_view, hashes.at(i),
                               ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(name_view))
                .is_ok();
        CRUCIBLE_FATAL_INVARIANT(was_registered);
    }

    bench::print("=== schema_table ===\n");
    bench::print("  table entries: {} (log2 = ~8 probes)\n\n", N);

    ::fixy::FixedArray<bench::Report, 3> reports{
        [&] {
            // The rotation (idx + 1) % N stays below N, so the compiler folds
            // the check of the index and the body holds no branch for it.
            // The index is below N, so its 32-bit value is exact, and the
            // remainder is the 32-bit remainder of a counter of 32 bits.
            ::fixy::Index<N> idx = ::fixy::index<0>();
            return bench::run("lookup hit  (250-entry table)", [&] {
                const auto h = hashes.at(idx);
                idx = ::fixy::Index<N>::check((::fixy::wrapping_narrow<uint32_t>(idx.raw()) + 1) % N)
                          .expect("a rotation modulo N stays below N");
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
            const auto hot = hashes.at(::fixy::index<N / 2>());
            return bench::run("short_name(strip aten::)", [&] {
                const bool is_found = table.short_name(hot).is_some();
                bench::do_not_optimize(is_found);
            });
        }(),
    };

    bench::emit_reports(reports.view(), json);
    return 0;
}
