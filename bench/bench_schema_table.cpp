// SchemaTable::lookup — sorted binary-search op-name resolution.
//
// Not on the bg drain hot path; diagnostic / visualization only. Still
// measured for parity with CKernelTable, because the two tables keep their
// entries in the order of the hashes.
//
// Repeated hash of a name table
//   The check of a trace file refuses a table with two names of one hash
//   (crucible/TraceLoader.h).  The rows time the test on TRACE_NAMES_MAX
//   distinct hashes in no order, the worst case, because no pair stops the
//   test early:
//     quadratic        the test of each hash against each hash before it,
//                      the old form: n * (n - 1) / 2 compares
//     sort and scan    has_repeated_hash_: the radix sort of the base and a
//                      scan of the neighbors, with the copy of the hashes
//                      into the frames of the sort
//     check of table   CheckedTraceImage::check of an image that holds the
//                      table of one-character names: the test of each name
//                      by sanitize_schema_name and the test of a repeated
//                      hash
//   The floor of the sort and scan is the passes of the radix sort over the
//   eight bytes of a key.  The sort reads each key one time for the counts,
//   and it moves each key one time for each byte in which two keys differ:
//   eight moves of 1024 keys of eight bytes, about 2 cycles for each key and
//   pass at a store a cycle, about 16 thousand cycles.

#include <cstddef>
#include <cstdint>

#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>

#include "bench_harness.h"

using crucible::SchemaHash;
using crucible::SchemaTable;

namespace {

// The spread of the hashes of the rows of a name table: an odd constant, so
// the products of the first TRACE_NAMES_MAX counts are distinct and in no
// order.
constexpr uint64_t kSpread = 0x9E3779B97F4A7C15ULL;

// True when two of the hashes are equal, by the test of each hash against
// each hash before it.  Complexity: n * (n - 1) / 2 compares.
[[nodiscard]] bool has_repeated_hash_quadratic(::fixy::View<uint64_t const> hashes) noexcept {
    std::size_t later = 0;
    for (const uint64_t hash : hashes) {
        std::size_t earlier = 0;
        for (const uint64_t held : hashes) {
            if (earlier == later) break;
            if (held == hash) return true;
            ++earlier;
        }
        ++later;
    }
    return false;
}

// The bytes of a trace with no op and a name table of TRACE_NAMES_MAX names
// of one character, on the hashes of the rows.
using NameTableImage = ::fixy::FixedList<std::byte, 16384>;

// Appends the bytes of value to image, the low byte first.
template <class Word>
void append_word(NameTableImage& image, Word value) noexcept {
    for (std::size_t shift = 0; shift < sizeof(Word) * 8; shift += 8) {
        const bool is_put = image.append(static_cast<std::byte>(static_cast<uint8_t>(value >> shift))).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
}

[[nodiscard]] NameTableImage make_name_table_image() noexcept {
    NameTableImage image{};
    const auto header = crucible::encode_trace_header(0, 0);
    for (const std::byte header_byte : header.view()) {
        const bool is_put = image.append(header_byte).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
    append_word<uint32_t>(image, crucible::TRACE_NAMES_MAX);
    for (uint64_t index = 1; index <= crucible::TRACE_NAMES_MAX; ++index) {
        append_word<uint64_t>(image, kSpread * index);
        append_word<uint16_t>(image, 1);
        const bool is_put = image.append(std::byte{'n'}).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
    return image;
}

}  // namespace

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

    crucible::detail::TraceNameHashes table_hashes{};
    for (uint64_t index = 1; index <= crucible::TRACE_NAMES_MAX; ++index) {
        const bool is_held = table_hashes.append(kSpread * index).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_held);
    }
    const NameTableImage image = make_name_table_image();

    bench::print("\n=== repeated hash of a name table ===\n");
    bench::print("  names: {}, each hash distinct\n\n", crucible::TRACE_NAMES_MAX);

    ::fixy::FixedArray<bench::Report, 3> table_reports{
        bench::run("quadratic (old)",
                   [&] {
                       const bool is_repeated = has_repeated_hash_quadratic(table_hashes.view());
                       bench::do_not_optimize(is_repeated);
                   }),
        bench::run("sort and scan (has_repeated_hash_)",
                   [&] {
                       const bool is_repeated = crucible::detail::has_repeated_hash_(table_hashes);
                       bench::do_not_optimize(is_repeated);
                   }),
        bench::run("check of table (CheckedTraceImage::check)",
                   [&] {
                       const bool is_checked =
                           crucible::CheckedTraceImage::check(::fixy::View<std::byte const>{image.view()}).is_ok();
                       bench::do_not_optimize(is_checked);
                   }),
    };

    bench::emit_reports(table_reports.view(), json);
    return 0;
}
