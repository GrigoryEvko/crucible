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
//
// Reserved hash of an outside name
//   The door of an outside name refuses a name whose hash is the hash of an
//   operator of the generated table with a different name
//   (is_reserved_for_other_name of vessel/torch/vessel_api_typed.h).  The
//   rows time the check of the first row of the table, of its last row, and
//   of a hash of no row, the case of each operator from outside:
//     walk             the walk of each row in table order, the old form:
//                      it stops at the row of the hash, so a miss reads
//                      each of the 3110 rows
//     search           aten_op_row_of: the search of the last hash of each
//                      of the 25 parts, then of the 128 hashes of one part
//   The floor of the search is its chain of dependent loads: at most 5 and 8
//   steps, each a load of an L1 or L2 line and a compare, then the load of
//   the row and the compare of the name, about 14 loads at 4 to 14 cycles,
//   with a mispredicted branch at about half of the steps.

#include <cstddef>
#include <cstdint>

#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>

#include "../vessel/torch/vessel_api_typed.h"
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

// The old form of is_reserved_for_other_name: the walk of each row of the
// generated table in table order, to the row of the hash.  Complexity:
// linear in the rows.
[[nodiscard]] bool is_reserved_by_walk(SchemaHash hash, ::fixy::TextView name) noexcept {
    for (const ::fixy::View<crucible::vessel::OpEntry const> part : crucible::vessel::aten_op_parts) {
        for (const crucible::vessel::OpEntry& row : part) {
            if (row.schema_hash == hash.raw())
                return !crucible::SchemaNameParts{name}.is_same_full_name(crucible::vessel::name_parts_of(row));
        }
    }
    return false;
}

// The full name of a row: its name, and a dot and its overload when the
// overload has characters.
using FullName = ::fixy::FixedText<crucible::SCHEMA_NAME_MAX>;

[[nodiscard]] FullName full_name_of(crucible::vessel::OpEntry const& row) noexcept {
    FullName name{};
    const bool is_joined =
        name.append(row.name).is_ok()
        && (row.overload.size() == 0 || (name.append('.').is_ok() && name.append(row.overload).is_ok()));
    CRUCIBLE_FATAL_INVARIANT(is_joined);
    return name;
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
        // The names are texts of the run time, so they come through the
        // validator of a name from outside.
        ::fixy::FixedText<16> name{};
        const ::fixy::TextView name_view =
            ::fixy::format(name, "aten::op{}", i.raw()).expect("aten::op and a number below 250 fit in 16 characters");
        const SchemaTable::SanitizedName sanitized =
            crucible::sanitize_schema_name(hashes.at(i), ::fixy::mint_tagged<::fixy::tags::source::External>(name_view))
                .expect("each name of the bench passes the validator");
        const bool was_registered = table
                                        .register_name(writer, ::foundation::effects::testing::test().alloc,
                                                       mutable_view, hashes.at(i), sanitized)
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

    constexpr crucible::vessel::OpEntry const& first_row = crucible::vessel::aten_op_row<0>();
    constexpr crucible::vessel::OpEntry const& last_row =
        crucible::vessel::aten_op_row<crucible::vessel::kAtenOpCount - 1>();
    const FullName first_name = full_name_of(first_row);
    const FullName last_name = full_name_of(last_row);
    const SchemaHash first_hash{first_row.schema_hash};
    const SchemaHash last_hash{last_row.schema_hash};
    constexpr ::fixy::TextView kOutsideName{"crucible::outside_op"};
    const SchemaHash outside_hash{0x5DEECE66DULL};
    CRUCIBLE_FATAL_INVARIANT(is_reserved_by_walk(outside_hash, kOutsideName) == false);
    CRUCIBLE_FATAL_INVARIANT(crucible::vessel::aten_op_index_of(outside_hash).is_none());

    bench::print("\n=== reserved hash of an outside name ===\n");
    bench::print("  rows of the generated table: {}\n\n", crucible::vessel::kAtenOpCount);

    // Each body takes copies of its hash and its name that the optimizer does
    // not know, so it runs the whole check in each call.
    ::fixy::FixedArray<bench::Report, 6> reserved_reports{
        bench::run("walk (old), first row",
                   [&] {
                       const bool is_reserved =
                           is_reserved_by_walk(bench::make_opaque(first_hash), bench::make_opaque(first_name.view()));
                       bench::do_not_optimize(is_reserved);
                   }),
        bench::run("walk (old), last row",
                   [&] {
                       const bool is_reserved =
                           is_reserved_by_walk(bench::make_opaque(last_hash), bench::make_opaque(last_name.view()));
                       bench::do_not_optimize(is_reserved);
                   }),
        bench::run("walk (old), no row",
                   [&] {
                       const bool is_reserved =
                           is_reserved_by_walk(bench::make_opaque(outside_hash), bench::make_opaque(kOutsideName));
                       bench::do_not_optimize(is_reserved);
                   }),
        bench::run("search, first row",
                   [&] {
                       const bool is_reserved = crucible::vessel::is_reserved_for_other_name(
                           bench::make_opaque(first_hash),
                           crucible::SchemaNameParts{bench::make_opaque(first_name.view())});
                       bench::do_not_optimize(is_reserved);
                   }),
        bench::run("search, last row",
                   [&] {
                       const bool is_reserved = crucible::vessel::is_reserved_for_other_name(
                           bench::make_opaque(last_hash),
                           crucible::SchemaNameParts{bench::make_opaque(last_name.view())});
                       bench::do_not_optimize(is_reserved);
                   }),
        bench::run("search, no row",
                   [&] {
                       const bool is_reserved = crucible::vessel::is_reserved_for_other_name(
                           bench::make_opaque(outside_hash),
                           crucible::SchemaNameParts{bench::make_opaque(kOutsideName)});
                       bench::do_not_optimize(is_reserved);
                   }),
    };

    bench::emit_reports(reserved_reports.view(), json);
    return 0;
}
