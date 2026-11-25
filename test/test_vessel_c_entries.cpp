// The C entries of libcrucible_vessel.so judge each pointer and count pair of
// a caller once, at the boundary, and give their refusal for a pair that
// breaks the rule of the arrays of vessel_api.h: the action REFUSED, or 0 with
// no write for an entry that writes into an array of the caller.  No pair
// ends the process at a contract check, and no pair records an op.  The test
// calls the entries through the C header, as a ctypes caller does.
#include "../vessel/torch/vessel_api.h"

#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceLoader.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/File.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace crucible::testing {

// The test door of a Sanitized name, which crucible/SchemaTable.h names as a
// friend of the name.  It plants a name on a hash that no check read, such as
// a name that holds a zero, which sanitize_schema_name refuses.  Then the
// test reaches the refusal of the export of a test build.  Only this test
// defines the door.
class UncheckedSchemaNameDoor final {
public:
    [[nodiscard]] static SanitizedSchemaName plant(SchemaHash hash, ::fixy::TextView characters) noexcept {
        return SanitizedSchemaName{hash,
                                   ::fixy::mint_tagged<::fixy::tags::source::External>(SchemaNameParts{characters})
                                       .retag<::fixy::tags::source::Sanitized>()};
    }
};

}  // namespace crucible::testing

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

int g_failures = 0;

// True in a test build, whose export checks each name of the tables again, as
// vessel_api.cpp reads the same switch: has_test_oracles of
// foundation/Platform.h.
constexpr bool kExportChecksNames = ::foundation::has_test_oracles;

#define EXPECT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::fixy::report(::fixy::Sink::Err, "FAIL: {} -- {} ({}:{})\n", ::fixy::TextView{#cond}, \
                           ::fixy::TextView{msg}, ::fixy::TextView{__FILE__}, __LINE__);           \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// The schema hashes of the two ops of the loop.  The C entries take each hash
// other than zero.
constexpr uint64_t kScaleSchema = 0x5CA1E0001ULL;
constexpr uint64_t kAddSchema = 0xADD00002ULL;

// The crucible ordinal of float32, and the tag of a double scalar.
constexpr int8_t kFloat = 6;
constexpr uint16_t kDoubleTag = 1;

// The most iterations of the loop that forms a region.  The Vigil activates a
// region after two periods of the loop.
constexpr int kMaxIterations = 60;

// The two records of one op: an input and an output, each a contiguous float
// tensor of four elements.
using OpRecords = ::fixy::FixedArray<CrucibleMeta, 2>;

// The records of the two ops of the loop.
struct LoopRecords {
    OpRecords scale{};
    OpRecords add{};
};

// The state of the Vigil at a check of the out arrays.
enum class RegionPhase : uint8_t {
    NoRegion,
    Active,
};

// The bytes of an array of the caller, so that a case can give a pointer one
// byte past an aligned element.
template <class Element, std::size_t Count>
struct CallerBytes {
    ::fixy::HeapArray<std::byte, alignof(Element)> bytes = ::fixy::mint_heap_array_zeroed<std::byte, alignof(Element)>(
        ::foundation::effects::testing::test().alloc, sizeof(Element) * Count + 1);
};

// The C entries take and give the raw pointers of the C interface, and a
// CrucibleMeta holds C arrays and a pointer, so each call of an entry and each
// write of a record is in an ABI region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the case calls the C entries of the vessel library with raw pointers")

// The record of a contiguous float tensor of four elements at a synthetic
// address.  No code reads through the address.
[[nodiscard]] CrucibleMeta record_at(crucible::SyntheticAddress address) noexcept {
    CrucibleMeta record{};
    record.sizes[0] = 4;
    record.strides[0] = 1;
    record.data_ptr = std::bit_cast<void*>(std::to_underlying(address));
    record.ndim = 1;
    record.dtype = kFloat;
    record.device_idx = -1;
    record.flags = 0x02;
    record.storage_nbytes = 16;
    return record;
}

// The place of the array of a caller in its bytes: the first byte, which has
// the alignment of the element, or one byte past it, which does not.
enum class Place : uint8_t {
    Aligned,
    OneBytePast,
};

// Calls entry with the array at place in bytes, and gives the result of
// entry.  The pointer stays in the call.  An entry that writes an element
// starts its life in the bytes, and no entry reads through a pointer one
// byte past the first byte.
template <class Element, std::size_t Count, class Entry>
[[nodiscard]] auto call_with_array(CallerBytes<Element, Count>& bytes, Place place, Entry&& entry) noexcept {
    auto [result, held] = ::fixy::with_mut(std::move(bytes.bytes), [place, entry](auto run) noexcept {
        std::byte* const first = ::foundation::core::foreign_first(run);
        return entry(std::bit_cast<Element*>(place == Place::Aligned ? first : first + 1));
    });
    bytes.bytes = std::move(held);
    return result;
}

// True when each byte of bytes is zero, so no entry wrote into them.
template <class Element, std::size_t Count>
[[nodiscard]] bool is_untouched(CallerBytes<Element, Count> const& bytes) noexcept {
    for (const std::byte byte : bytes.bytes.view()) {
        if (byte != std::byte{0}) return false;
    }
    return true;
}

// One iteration of the loop: a scale with one data scalar, then an add.
void run_iteration(CrucibleHandle handle, LoopRecords const& loop) noexcept {
    const ::fixy::FixedArray<int64_t, 5> step{std::bit_cast<int64_t>(0.5), 0, 0, 0, 0};
    static_cast<void>(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, &loop.scale.at(::fixy::index<0>()), 2,
                                              &step.at(::fixy::index<0>()), 1, kDoubleTag, 1, 0, 0));
    static_cast<void>(crucible_dispatch_op(handle, kAddSchema, 1, 1, &loop.add.at(::fixy::index<0>()), 2));
}

// The pairs of the two dispatch entries.  The handle has no active region.
void test_dispatch_pairs(CrucibleHandle handle) {
    const OpRecords records{record_at(crucible::SyntheticAddress{0x10000}),
                            record_at(crucible::SyntheticAddress{0x20000})};
    const CrucibleMeta* const metas = &records.at(::fixy::index<0>());
    const ::fixy::FixedArray<int64_t, 5> values{1, 2, 3, 4, 5};
    const int64_t* const scalars = &values.at(::fixy::index<0>());
    CallerBytes<CrucibleMeta, 1> meta_bytes{};
    CallerBytes<int64_t, 1> scalar_bytes{};
    const auto misaligned_meta_action = [handle](CrucibleMeta* record) noexcept {
        return crucible_dispatch_op(handle, kAddSchema, 1, 0, record, 1).action;
    };
    const auto misaligned_meta_ex_action = [handle, scalars](CrucibleMeta* record) noexcept {
        return crucible_dispatch_op_ex(handle, kScaleSchema, 1, 0, record, 1, scalars, 1, 0, 0, 0, 0).action;
    };
    const auto misaligned_scalar_action = [handle, metas](int64_t* values_at) noexcept {
        return crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, values_at, 1, 0, 0, 0, 0).action;
    };

    // The cases that the old entries recorded or judged with a contract
    // check come first, in the order of their defect.
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, nullptr, 5, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_REFUSED,
           "a null scalar array with a count above 0 is refused, not recorded with zero values");
    EXPECT(call_with_array(scalar_bytes, Place::OneBytePast, misaligned_scalar_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned scalar array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op(handle, kAddSchema, 0, 0, metas, 0).action == CRUCIBLE_ACTION_RECORD,
           "a meta array with a count of 0 reads no record, and the op with no tensor records");
    EXPECT(call_with_array(meta_bytes, Place::OneBytePast, misaligned_meta_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned meta array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op(handle, kAddSchema, 1, 1, nullptr, 2).action == CRUCIBLE_ACTION_REFUSED,
           "a null meta array with a count above 0 is refused");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, scalars, 0, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a scalar buffer with a count of 0 reads no value, as crucible_mode.py gives it");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, metas, 2, nullptr, 0, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a null scalar array with a count of 0 records");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 1, 1, nullptr, 2, scalars, 1, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_REFUSED,
           "a null meta array with a count above 0 is refused by the extended entry");
    EXPECT(crucible_dispatch_op_ex(handle, kScaleSchema, 0, 0, metas, 0, scalars, 1, 0, 0, 0, 0).action
               == CRUCIBLE_ACTION_RECORD,
           "a meta array with a count of 0 reads no record in the extended entry");
    EXPECT(call_with_array(meta_bytes, Place::OneBytePast, misaligned_meta_ex_action) == CRUCIBLE_ACTION_REFUSED,
           "a misaligned meta array with a count above 0 is refused by the extended entry");
}

// The pairs of the two entries that write into an array of the caller, with
// and with no active region.
void test_out_pairs(CrucibleHandle handle, RegionPhase phase) {
    CallerBytes<CrucibleSlot, 8> slots{};
    CallerBytes<CrucibleBoundScalar, 4> bound{};
    CallerBytes<CrucibleSlot, 4> slot_bytes{};
    CallerBytes<CrucibleBoundScalar, 4> bound_bytes{};

    const auto slots_with = [handle](uint32_t capacity) noexcept {
        return [handle, capacity](CrucibleSlot* out) noexcept {
            return crucible_active_region_slots(handle, out, capacity);
        };
    };
    const auto bound_with = [handle](uint32_t capacity) noexcept {
        return [handle, capacity](CrucibleBoundScalar* out) noexcept {
            return crucible_bound_scalars(handle, out, capacity);
        };
    };

    const uint32_t slot_count = crucible_active_region_slots(handle, nullptr, 0);
    const uint32_t bound_count = crucible_bound_scalars(handle, nullptr, 0);
    EXPECT(crucible_active_region_slots(handle, nullptr, 4) == 0, "a null out array with a capacity above 0 gives 0");
    EXPECT(call_with_array(slot_bytes, Place::OneBytePast, slots_with(4)) == 0,
           "a misaligned out array with a capacity above 0 gives 0");
    EXPECT(is_untouched(slot_bytes), "the entry writes nothing into a misaligned out array");
    EXPECT(call_with_array(slots, Place::Aligned, slots_with(0)) == slot_count,
           "an out array with a capacity of 0 is a read of the count");
    EXPECT(call_with_array(slots, Place::Aligned, slots_with(8)) == slot_count,
           "an aligned out array with a capacity above 0 gives the count");

    EXPECT(crucible_bound_scalars(handle, nullptr, 4) == 0, "a null out array of bound scalars gives 0");
    EXPECT(call_with_array(bound_bytes, Place::OneBytePast, bound_with(4)) == 0,
           "a misaligned out array of bound scalars gives 0");
    EXPECT(is_untouched(bound_bytes), "the entry writes nothing into a misaligned out array of bound scalars");
    EXPECT(call_with_array(bound, Place::Aligned, bound_with(4)) == bound_count,
           "an aligned out array of bound scalars gives the count");

    // With an active region the plan has slots and the scale has a data slot,
    // so a refusal and a count of zero differ.
    if (phase == RegionPhase::Active) {
        EXPECT(slot_count > 0, "the plan of the active region has slots");
        EXPECT(bound_count > 0, "the active region has a data slot");
    }
}

// The file of the exports of the test, in the working directory of the test.
constexpr ::fixy::CText kExportFile{"crtrace-c-entries.bin"};

// The hashes of the names of the export case.
constexpr uint64_t kLongSchema = 0x10E6000000000001ULL;
constexpr uint64_t kTooLongSchema = 0x10E6000000000002ULL;
constexpr uint64_t kEmptyStoredSchema = 0x10E6000000000003ULL;

// The bytes that the export writes for the name of kLongSchema: the hash, the
// length and SCHEMA_NAME_MAX characters n.
[[nodiscard]] ::fixy::FixedText<crucible::SCHEMA_NAME_MAX + 16> long_name_record() noexcept {
    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX + 16> record{};
    const auto hash_bytes = ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(uint64_t)>>(kLongSchema);
    const auto length_bytes =
        ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(uint16_t)>>(uint16_t{crucible::SCHEMA_NAME_MAX});
    for (const std::byte byte : hash_bytes.view())
        EXPECT(record.append(static_cast<char>(byte)).is_ok(), "the record holds the bytes of the hash");
    for (const std::byte byte : length_bytes.view())
        EXPECT(record.append(static_cast<char>(byte)).is_ok(), "the record holds the bytes of the length");
    for (std::size_t character = 0; character < crucible::SCHEMA_NAME_MAX; ++character)
        EXPECT(record.append('n').is_ok(), "the record holds the characters of the name");
    return record;
}

// True when the file of the exports holds the bytes of record.  A file that
// does not read holds nothing.  The text of the file holds four names of
// SCHEMA_NAME_MAX characters, so the function keeps its own frame, and the
// frame of its caller stays below the limit of the stack of one function.
template <std::size_t Capacity>
[[nodiscard, gnu::noinline]] bool export_file_holds(::fixy::FixedText<Capacity> const& record) noexcept {
    ::fixy::FixedText<4 * crucible::SCHEMA_NAME_MAX> file{};
    if (::foundation::os::read_file(kExportFile, file).is_err()) return false;
    return ::fixy::find(file.view(), record.view()).is_some();
}

// True when the file of the exports exists and reads.  The function keeps its
// own frame, as export_file_holds does.
[[nodiscard, gnu::noinline]] bool export_file_exists() noexcept {
    ::fixy::FixedText<4 * crucible::SCHEMA_NAME_MAX> file{};
    return ::foundation::os::read_file(kExportFile, file).is_ok();
}

// Removes the file of the exports from the working directory of the test.
void remove_export_file() noexcept {
    const auto here = ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::TextView{"."}))
                          .expect("the working directory passes the path check");
    const ::fixy::fs::DirectoryHandle directory =
        ::fixy::fs::open_directory(kTest, here).expect("the working directory opens");
    EXPECT(::fixy::fs::unlink_at(kTest, directory.ref(), kExportFile).is_ok(), "the file of the exports goes");
}

// The C entry and the export take a name of SCHEMA_NAME_MAX characters, and
// the loader reads the file of the export.  The C entry refuses one character
// more.  The table refuses an internal name that starts with a zero, so only
// a Sanitized name that no validator made puts an empty name in the table.
// The export of a test build refuses such a name, because the loader refuses
// an empty name: the export gives 0 and writes no file.  A Release build has
// no such check, so the case of the planted name runs only in a test build.
// The old export wrote the file with the length 0, and the old C entry
// refused each name past 256 characters.
void test_export_writes_only_names_that_the_loader_takes(CrucibleHandle handle) {
    // Each text holds its characters and the zero that ends a C text.
    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX + 1> longest{};
    ::fixy::FixedText<crucible::SCHEMA_NAME_MAX + 2> too_long{};
    for (std::size_t character = 0; character < crucible::SCHEMA_NAME_MAX; ++character)
        EXPECT(longest.append('n').is_ok() && too_long.append('n').is_ok(), "each text holds the characters");
    EXPECT(longest.append('\0').is_ok() && too_long.append('n').is_ok() && too_long.append('\0').is_ok(),
           "each text holds its end");
    const ::fixy::CText longest_name = ::fixy::mint_c_text(longest.view()).expect("the text ends with its zero");
    const ::fixy::CText too_long_name = ::fixy::mint_c_text(too_long.view()).expect("the text ends with its zero");
    EXPECT(crucible_register_schema_name(handle, kLongSchema, ::foundation::core::foreign_c_str(longest_name))
               == CRUCIBLE_SCHEMA_NAME_REGISTERED,
           "the C entry takes a name of SCHEMA_NAME_MAX characters");
    EXPECT(crucible_register_schema_name(handle, kTooLongSchema, ::foundation::core::foreign_c_str(too_long_name))
               == CRUCIBLE_SCHEMA_NAME_REFUSED,
           "the C entry refuses a name of one character past SCHEMA_NAME_MAX");

    EXPECT(crucible_export_crtrace(handle, ::foundation::core::foreign_c_str(kExportFile)) == 1,
           "the export writes the longest name");
    EXPECT(export_file_holds(long_name_record()), "the file holds the hash, the length and the longest name");
    const auto path = ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(kExportFile.view()))
                          .expect("the file name passes the path check");
    EXPECT(crucible::load_trace(kTest, path).is_ok(), "the loader reads the file of the export");
    remove_export_file();

    // The tables are process-wide, so the export of the library reads a name
    // that this binary registers.  The global table is sealed, so the name
    // goes to the late table.  The test door skips sanitize_schema_name,
    // which refuses a name that holds a zero.
    constexpr crucible::VigilFgCtx kForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();
    if (kExportChecksNames) {
        ::fixy::FixedText<8> leading_zero{};
        EXPECT(leading_zero.append('\0').is_ok() && leading_zero.append('a').is_ok()
                   && leading_zero.append('b').is_ok(),
               "the text holds a zero and two letters");
        EXPECT(leading_zero.size() == 3, "the view of the name holds the zero");
        const crucible::SanitizedSchemaName unchecked = crucible::testing::UncheckedSchemaNameDoor::plant(
            crucible::SchemaHash{kEmptyStoredSchema}, leading_zero.view());
        EXPECT(crucible::register_schema_name(kForeground, ::foundation::effects::testing::test().alloc, unchecked)
                   .is_ok(),
               "the table takes the Sanitized name");
        const auto stored = crucible::schema_name(crucible::SchemaHash{kEmptyStoredSchema});
        const bool is_stored_empty = stored.match(
            [](crucible::InternedSchemaName const& found) noexcept { return found.value().view().size() == 0; },
            [] noexcept { return false; });
        EXPECT(is_stored_empty, "the table keeps the Sanitized name as an empty name");

        EXPECT(crucible_export_crtrace(handle, ::foundation::core::foreign_c_str(kExportFile)) == 0,
               "the export of a test build refuses an empty name");
        EXPECT(!export_file_exists(), "a refused export writes no file");
    }

    // A table with no such name exports again.  The late table takes more
    // names than the 1024 of two tables of SCHEMA_TABLE_CAP names, the export
    // writes each of them, and the loader reads the file.  The old late table
    // gave the full refusal to the 513th name, and the old loader refused a
    // name table of more than 1024 names.
    crucible::late_schema_table().clear(::foundation::effects::testing::test());
    constexpr uint32_t kLateNames = 2 * crucible::SCHEMA_TABLE_CAP + 76;
    constexpr uint64_t kFirstLateHash = 0x1A7E000000000000ULL;
    for (uint32_t index = 0; index < kLateNames; ++index) {
        ::fixy::FixedText<32> name{};
        static_cast<void>(::fixy::format(name, "aten::late_{}", index).expect("the name fits in its text"));
        const crucible::SchemaHash hash{kFirstLateHash + index};
        const crucible::SanitizedSchemaName late_name =
            crucible::sanitize_schema_name(hash, ::fixy::mint_tagged<::fixy::tags::source::ABIBoundary>(name.view()))
                .expect("each name of the case passes the validator");
        EXPECT(crucible::register_schema_name(kForeground, ::foundation::effects::testing::test().alloc, late_name)
                   .is_ok(),
               "the late table takes each name");
    }
    EXPECT(crucible_export_crtrace(handle, ::foundation::core::foreign_c_str(kExportFile)) == 1,
           "the export writes a table of valid names");
    const auto many_path =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(kExportFile.view()))
            .expect("the file name passes the path check");
    EXPECT(crucible::load_trace(kTest, many_path).is_ok(), "the loader reads a table of more than 1024 names");
    remove_export_file();
    crucible::late_schema_table().clear(::foundation::effects::testing::test());
}

// The pairs of a handle with no region, from create to destroy.
void test_pairs_with_no_region() {
    const CrucibleHandle handle = crucible_create(0);
    if (handle == nullptr) {
        EXPECT(handle != nullptr, "crucible_create gives a handle for the eager word 0");
        return;
    }
    test_dispatch_pairs(handle);
    test_out_pairs(handle, RegionPhase::NoRegion);
    crucible_destroy(handle);
}

// The pairs of the out arrays of a handle that replays a region.  A loop of
// two ops over three tensors runs until the Vigil replays its region.  The
// records of the loop are the same in each iteration.
void test_pairs_with_a_region() {
    const CrucibleHandle handle = crucible_create(0);
    if (handle == nullptr) {
        EXPECT(handle != nullptr, "crucible_create gives a handle for the eager word 0");
        return;
    }
    const LoopRecords loop{
        .scale = {record_at(crucible::SyntheticAddress{0x10000}), record_at(crucible::SyntheticAddress{0x20000})},
        .add = {record_at(crucible::SyntheticAddress{0x20000}), record_at(crucible::SyntheticAddress{0x30000})}};
    bool is_compiled = false;
    for (int iteration = 0; iteration < kMaxIterations && !is_compiled; ++iteration) {
        run_iteration(handle, loop);
        is_compiled = crucible_is_compiled(handle) == 1;
        if (!is_compiled) static_cast<void>(crucible_flush(handle));
    }
    EXPECT(is_compiled, "the loop of the C entries replays a region");
    if (is_compiled) test_out_pairs(handle, RegionPhase::Active);
    if (is_compiled) test_export_writes_only_names_that_the_loader_takes(handle);
    crucible_destroy(handle);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    test_pairs_with_no_region();
    test_pairs_with_a_region();
    if (g_failures != 0) {
        ::fixy::report(::fixy::Sink::Err, "test_vessel_c_entries: FAIL ({})\n", g_failures);
        return EXIT_FAILURE;
    }
    ::fixy::check::pass(kTest, "test_vessel_c_entries: PASS\n");
    return EXIT_SUCCESS;
}
