// SchemaTable::lookup — sorted binary-search op-name resolution.
//
// Not on the bg drain hot path; diagnostic / visualization only. Still
// measured for parity with CKernelTable, because the two tables keep their
// entries in the order of the hashes.
//
// Repeated hash of a name table
//   The check of a trace file refuses a table with two names of one hash
//   (crucible/TraceLoader.h).  The rows time the test on 1024 distinct
//   hashes in no order, the worst case, because no pair stops the test
//   early:
//     quadratic        the test of each hash against each hash before it,
//                      the old form: n * (n - 1) / 2 compares
//     sort and scan    has_repeated_hash_: the radix sort of the base and a
//                      scan of the neighbors, with the copy of the hashes
//                      and the room of the sort on the heap
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
//   operator of the generated table with a different name (the reserve
//   kAtenNameReserve of vessel/torch/vessel_api_typed.h).  The
//   rows time the check of the first row of the table, of its last row, and
//   of a hash of no row, the case of each operator from outside:
//     walk             the walk of each row in table order, the old form:
//                      it stops at the row of the hash, so a miss reads
//                      each of the 3110 rows
//     search           the reserve, through aten_op_index_of and a call of
//                      its CallRef: the search of the last hash of each
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
// the products of the first kTimedNames counts are distinct and in no order.
constexpr uint64_t kSpread = 0x9E3779B97F4A7C15ULL;

// The names of the timed name table: the names of two tables of
// SCHEMA_TABLE_CAP names.  The quadratic row of a table of TRACE_NAMES_MAX
// names would take seconds for each run.
constexpr uint32_t kTimedNames = 2 * crucible::SCHEMA_TABLE_CAP;

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

// The bytes of a trace with no op and a name table of kTimedNames names of
// one character, on the hashes of the rows.
using NameTableImage = ::fixy::FixedList<std::byte, 16384>;

// Appends the bytes of value to image, the low byte first.
template <class Word>
void append_word(NameTableImage& image, Word value) noexcept {
    for (std::size_t shift = 0; shift < sizeof(Word) * 8; shift += 8) {
        const bool is_put = image.append(static_cast<std::byte>(static_cast<uint8_t>(value >> shift))).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
}

// The old form of the reserve of the generated table: the walk of each row of
// the generated table in table order, to the row of the hash.  Complexity:
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
    append_word<uint32_t>(image, kTimedNames);
    for (uint64_t index = 1; index <= kTimedNames; ++index) {
        append_word<uint64_t>(image, kSpread * index);
        append_word<uint16_t>(image, 1);
        const bool is_put = image.append(std::byte{'n'}).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_put);
    }
    return image;
}

// The state of each row lives in a home of the row (bench::RowHome), so the
// distance from the state to the frame of the timed loop depends only on the
// code of the row.

// The count of the names of the table of the lookup rows.
constexpr uint32_t kTableNames = 250;

// The allocation capability of the rows of the name table.
constexpr ::foundation::effects::Alloc kAllocation = ::foundation::effects::testing::test().alloc;

// The key of a row that misses, which the body steps in each call.
struct MissKey {
    uint64_t miss_key = 0;
};

// A table of kTableNames names, their hashes, the position of the next hash,
// the hash in the middle, and the key of a miss.
struct LookupRow {
    SchemaTable table{};
    ::fixy::FixedArray<SchemaHash, kTableNames> hashes{};
    ::fixy::Index<kTableNames> idx = ::fixy::index<0>();
    SchemaHash hot{};
    uint64_t miss_key = 0xDEADBEEFCAFEBABEULL;

    LookupRow() noexcept {
        // The view and each registration ask for the context of a Vigil's
        // producer claim.  The bench uses no Vigil, so it takes that context
        // from the test door.  The bench has one writer, so the writer of the
        // claim takes the section with one compare-exchange.
        constexpr crucible::VigilFgCtx writer = ::foundation::effects::testing::foreground<crucible::Vigil>();
        const auto mutable_view = table.mint_mutable_view(writer).expect("a fresh table mints a mutable view");
        for (::fixy::Index<kTableNames> const i : hashes.indices()) {
            const uint64_t h = 0x9E3779B97F4A7C15ULL * (i.raw() + 1);
            hashes.set(i, SchemaHash{h});
            // The names are texts of the run time, so they come through the
            // validator of a name from outside.
            ::fixy::FixedText<16> name{};
            const ::fixy::TextView name_view = ::fixy::format(name, "aten::op{}", i.raw())
                                                   .expect("aten::op and a number below 250 fit in 16 characters");
            const crucible::SanitizedSchemaName sanitized =
                crucible::sanitize_schema_name(hashes.at(i),
                                               ::fixy::mint_tagged<::fixy::tags::source::External>(name_view))
                    .expect("each name of the bench passes the validator");
            const bool was_registered =
                crucible::register_schema_name_in(table, writer, kAllocation, mutable_view, sanitized).is_ok();
            CRUCIBLE_FATAL_INVARIANT(was_registered);
        }
        hot = hashes.at(::fixy::index<kTableNames / 2>());
    }
};

// The hashes of a name table of kTimedNames names, and the image of a trace
// with that table.
struct NameTableRow {
    ::fixy::FixedList<uint64_t, kTimedNames> table_hashes{};
    NameTableImage image = make_name_table_image();

    NameTableRow() noexcept {
        for (uint64_t index = 1; index <= kTimedNames; ++index) {
            const bool is_held = table_hashes.append(kSpread * index).is_ok();
            CRUCIBLE_FATAL_INVARIANT(is_held);
        }
    }
};

// The name that no row of the generated table holds, and its hash.
constexpr ::fixy::TextView kOutsideName{"crucible::outside_op"};
constexpr SchemaHash kOutsideHash{0x5DEECE66DULL};

// The names and the hashes of the first and the last row of the generated
// table, and the hash of no row.
struct ReserveRow {
    FullName first_name = full_name_of(crucible::vessel::aten_op_row<0>());
    FullName last_name = full_name_of(crucible::vessel::aten_op_row<crucible::vessel::kAtenOpCount - 1>());
    SchemaHash first_hash{crucible::vessel::aten_op_row<0>().schema_hash};
    SchemaHash last_hash{crucible::vessel::aten_op_row<crucible::vessel::kAtenOpCount - 1>().schema_hash};
    SchemaHash outside_hash = kOutsideHash;
};

// The report of a row whose step runs on a name table in a home of its own.
template <typename Step>
[[nodiscard]] bench::Report run_on_name_table(::fixy::TextView name, Step step) {
    return bench::run_in_home(name, []() noexcept { return NameTableRow{}; }, step);
}

// The report of a row whose step runs on the reserve rows in a home of its
// own.
template <typename Step>
[[nodiscard]] bench::Report run_on_reserve(::fixy::TextView name, Step step) {
    return bench::run_in_home(name, []() noexcept { return ReserveRow{}; }, step);
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== schema_table ===\n");
    bench::print("  table entries: {} (log2 = ~8 probes)\n\n", kTableNames);

    ::fixy::FixedArray<bench::Report, 4> reports{
        // The lookup of the record path: the global table, then the late
        // table, each a static of its door.  No name is in the two tables.
        bench::run_in_home(
            "schema_name miss (the two static tables)", []() noexcept { return MissKey{.miss_key = 0x5DEECE66DULL}; },
            [](MissKey& row) noexcept {
                row.miss_key = row.miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
                const bool is_found = crucible::schema_name(SchemaHash{row.miss_key}).is_some();
                bench::do_not_optimize(is_found);
            }),
        // The rotation (idx + 1) % N stays below N, so the compiler folds the
        // check of the index and the body holds no branch for it.  The index
        // is below N, so its 32-bit value is exact, and the remainder is the
        // 32-bit remainder of a counter of 32 bits.
        bench::run_in_home(
            "lookup hit  (250-entry table)", []() noexcept { return LookupRow{}; },
            [](LookupRow& row) noexcept {
                const auto h = row.hashes.at(row.idx);
                row.idx = ::fixy::Index<kTableNames>::check((::fixy::wrapping_narrow<uint32_t>(row.idx.raw()) + 1)
                                                            % kTableNames)
                              .expect("a rotation modulo N stays below N");
                const bool is_found = row.table.lookup(h).is_some();
                bench::do_not_optimize(is_found);
            }),
        bench::run_in_home(
            "lookup miss (returns no name)", []() noexcept { return LookupRow{}; },
            [](LookupRow& row) noexcept {
                row.miss_key = row.miss_key * 6364136223846793005ULL + 1442695040888963407ULL;
                const bool is_found = row.table.lookup(SchemaHash{row.miss_key}).is_some();
                bench::do_not_optimize(is_found);
            }),
        bench::run_in_home(
            "short_name(strip aten::)", []() noexcept { return LookupRow{}; },
            [](LookupRow& row) noexcept {
                const bool is_found = row.table.short_name(row.hot).is_some();
                bench::do_not_optimize(is_found);
            }),
    };

    bench::emit_reports(reports.view(), json);

    bench::print("\n=== repeated hash of a name table ===\n");
    bench::print("  names: {}, each hash distinct\n\n", kTimedNames);

    ::fixy::FixedArray<bench::Report, 3> table_reports{
        run_on_name_table("quadratic (old)",
                          [](NameTableRow& row) noexcept {
                              const bool is_repeated = has_repeated_hash_quadratic(row.table_hashes.view());
                              bench::do_not_optimize(is_repeated);
                          }),
        run_on_name_table("sort and scan (has_repeated_hash_)",
                          [](NameTableRow& row) noexcept {
                              const bool is_repeated = crucible::detail::has_repeated_hash_(
                                  kAllocation,
                                  ::fixy::mint_heap_array_copy<uint64_t>(kAllocation, row.table_hashes.view()));
                              bench::do_not_optimize(is_repeated);
                          }),
        run_on_name_table("check of table (CheckedTraceImage::check)",
                          [](NameTableRow& row) noexcept {
                              const bool is_checked = crucible::CheckedTraceImage::check(
                                                          ::fixy::View<std::byte const>{row.image.view()}, kAllocation)
                                                          .is_ok();
                              bench::do_not_optimize(is_checked);
                          }),
    };

    bench::emit_reports(table_reports.view(), json);

    CRUCIBLE_FATAL_INVARIANT(is_reserved_by_walk(kOutsideHash, kOutsideName) == false);
    CRUCIBLE_FATAL_INVARIANT(crucible::vessel::aten_op_index_of(kOutsideHash).is_none());

    bench::print("\n=== reserved hash of an outside name ===\n");
    bench::print("  rows of the generated table: {}\n\n", crucible::vessel::kAtenOpCount);

    // Each body takes copies of its hash and its name that the optimizer does
    // not know, so it runs the whole check in each call.
    ::fixy::FixedArray<bench::Report, 6> reserved_reports{
        run_on_reserve("walk (old), first row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = is_reserved_by_walk(bench::make_opaque(row.first_hash),
                                                                        bench::make_opaque(row.first_name.view()));
                           bench::do_not_optimize(is_reserved);
                       }),
        run_on_reserve("walk (old), last row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = is_reserved_by_walk(bench::make_opaque(row.last_hash),
                                                                        bench::make_opaque(row.last_name.view()));
                           bench::do_not_optimize(is_reserved);
                       }),
        run_on_reserve("walk (old), no row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = is_reserved_by_walk(bench::make_opaque(row.outside_hash),
                                                                        bench::make_opaque(kOutsideName));
                           bench::do_not_optimize(is_reserved);
                       }),
        run_on_reserve("search, first row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = crucible::vessel::kAtenNameReserve.is_reserved_for_other_name(
                               bench::make_opaque(row.first_hash),
                               crucible::SchemaNameParts{bench::make_opaque(row.first_name.view())});
                           bench::do_not_optimize(is_reserved);
                       }),
        run_on_reserve("search, last row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = crucible::vessel::kAtenNameReserve.is_reserved_for_other_name(
                               bench::make_opaque(row.last_hash),
                               crucible::SchemaNameParts{bench::make_opaque(row.last_name.view())});
                           bench::do_not_optimize(is_reserved);
                       }),
        run_on_reserve("search, no row",
                       [](ReserveRow& row) noexcept {
                           const bool is_reserved = crucible::vessel::kAtenNameReserve.is_reserved_for_other_name(
                               bench::make_opaque(row.outside_hash),
                               crucible::SchemaNameParts{bench::make_opaque(kOutsideName)});
                           bench::do_not_optimize(is_reserved);
                       }),
    };

    bench::emit_reports(reserved_reports.view(), json);
    return 0;
}
