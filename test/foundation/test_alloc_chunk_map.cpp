// The window door of foundation/alloc/Window.h and the chunk map of
// foundation/alloc/ChunkMap.h, on the host.
//
//   - The ladder with a cap of 4 GiB gives a window of 4 GiB, with no end of
//     the process, and the window with no cap has one size of the ladder.
//   - A cap below the smallest size gives WindowFull and the remedy text in
//     the diagnostic, and the next call that succeeds empties it.
//   - The map of a new window reads no owner for each chunk.  A write of an
//     entry reads back through the record, and an address outside the
//     window reads none.
//   - On aarch64 an address with a tag byte reads the chunk and the owner of
//     the same address with no tag.  On x86-64 the checked read refuses it.
//   - The records of the first and of the last chunk and unit are inside the
//     region, and each starts as zero bytes.
//   - A read of the guard chunk of an extent ends the child by SIGSEGV, and
//     a read of the chunk below it gives zero.
//   - The count of the lines of /proc/self/maps inside the window and its
//     region does not grow with the chunks of a committed extent.  After 4096
//     chunks of 9 extents it grows by at most two lines for each extent and
//     for each section of records.  A line of another mapping, such as a map
//     of a sanitizer for itself, does not count.  Under qemu-user, the two
//     counts skip, and each other check of the case runs.
//   - publish_host_window fills the host slot and no other slot.
//   - publish_device_window fills the 63 slots of the devices in order, and
//     the next device gets table_full with no end of the process.

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/Window.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Signal.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/File.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;
namespace os = ::foundation::os;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies_by;
using ::fixy::check::pass;
using ::fixy::signal::FatalSignal;
using ::foundation::core::ByteCount;

constexpr std::size_t kGibibyte = std::size_t{1} << 30;

// True when the test runs under qemu-user (test/foundation/CMakeLists.txt sets
// the definition).  qemu-user gives one line of the /proc/self/maps of the
// guest for each mprotect call, but the kernel merges the adjacent ranges of
// one protection into one line.  Under the emulator, the line counts of
// mappings_follow_extents then grow with the chunks, and the two checks of
// the counts skip.  Each other check of the case runs.
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

// The text of /proc/self/maps.  The buffer is static, so a read adds no
// mapping of its own.
constexpr std::size_t kMapsBytes = std::size_t{1} << 20;
constinit ::foundation::core::FixedText<kMapsBytes> maps_text{};

[[nodiscard]] alloc::Window reserve_capped(std::size_t cap) noexcept {
    alloc::WindowDiagnostic diagnostic{};
    return alloc::reserve_window(os::ReserveCap{cap}, ::foundation::core::to(diagnostic))
        .expect("the ladder gives a window under the cap");
}

[[nodiscard]] alloc::Window reserve_any() noexcept {
    alloc::WindowDiagnostic diagnostic{};
    return alloc::reserve_window(::foundation::core::to(diagnostic))
        .expect("the host gives a window of one size of the ladder");
}

[[nodiscard]] bool holds_text(alloc::WindowDiagnostic const& diagnostic, ::foundation::core::TextView part) noexcept {
    return ::foundation::core::find(diagnostic.text(), part).is_some();
}

// The count of the lines of /proc/self/maps whose first address lies in the
// window or in its region: one line for each mapping of the two
// reservations.  A sanitizer or the C library can map memory for itself at
// each time, so the lines of other mappings do not count.  Complexity: O(n)
// in the bytes of the file.
[[nodiscard]] std::size_t count_window_mappings(TestRunnerCtx const& test, alloc::WindowRecord record) noexcept {
    CRUCIBLE_TEST_ASSERT(test, os::read_file("/proc/self/maps", maps_text).is_ok());
    std::uint64_t const window_base = static_cast<std::uint64_t>(record.base());
    std::uint64_t const region_base = static_cast<std::uint64_t>(record.region_base());
    std::size_t lines = 0;
    std::uint64_t start = 0;
    bool is_reading_start = true;
    for (char const character : maps_text.view()) {
        if (character == '\n') {
            start = 0;
            is_reading_start = true;
            continue;
        }
        if (!is_reading_start) continue;
        if (character == '-') {
            is_reading_start = false;
            bool const is_in_window = start - window_base < record.length().raw();
            bool const is_in_region = start - region_base < record.region_length().raw();
            if (is_in_window || is_in_region) ++lines;
            continue;
        }
        std::uint64_t const digit = character <= '9' ? static_cast<std::uint64_t>(character - '0')
                                                     : static_cast<std::uint64_t>(character - 'a' + 10);
        start = start * 16 + digit;
    }
    return lines;
}

// Reads the first byte of a chunk of the window.
[[nodiscard]] std::uint8_t read_first_byte(alloc::WindowRecord record, alloc::ChunkIndex chunk) noexcept {
    std::uint64_t const address = static_cast<std::uint64_t>(record.address_of(chunk));
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a read of a byte of the window shows the access of its page")
    std::uint8_t const value = *__builtin_bit_cast(std::uint8_t const volatile*, address);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return value;
}

void ladder_under_a_cap(TestRunnerCtx const& test) {
    alloc::Window const window = reserve_capped(4 * kGibibyte);
    CRUCIBLE_TEST_ASSERT(test, window.record().length().raw() == 4 * kGibibyte);
    CRUCIBLE_TEST_ASSERT(test, window.record().chunk_count() == 2048);
    pass(test, "  ladder_under_a_cap: PASSED\n");
}

// THE FULL WINDOW of foundation/alloc/Window.h: a cap below the smallest
// size of the ladder gives WindowFull, with no end of the process, and the
// diagnostic names the sizes, the error and the limits to raise.  The next
// call that succeeds empties the diagnostic.
void full_window_gives_the_remedy(TestRunnerCtx const& test) {
    alloc::WindowDiagnostic diagnostic{};
    bool const is_full =
        alloc::reserve_window(os::ReserveCap{kGibibyte / 2}, ::foundation::core::to(diagnostic))
            .match([](alloc::Window) noexcept { return false; },
                   [](alloc::WindowFull full) noexcept { return full == alloc::WindowFull::no_address_space; });
    CRUCIBLE_TEST_ASSERT(test, is_full);
    CRUCIBLE_TEST_ASSERT(test, holds_text(diagnostic, "GiB to 1 GiB of address space, the last with enomem."));
    CRUCIBLE_TEST_ASSERT(test, holds_text(diagnostic, "(ulimit -v) or vm.max_map_count"));
    alloc::Window const window =
        alloc::reserve_window(os::ReserveCap{4 * kGibibyte}, ::foundation::core::to(diagnostic))
            .expect("the ladder gives a window under the cap");
    CRUCIBLE_TEST_ASSERT(test, window.record().length().raw() == 4 * kGibibyte);
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
    pass(test, "  full_window_gives_the_remedy: PASSED\n");
}

void ladder_with_no_cap(TestRunnerCtx const& test) {
    alloc::Window const window = reserve_any();
    std::size_t const length = window.record().length().raw();
    bool is_rung = false;
    for (std::size_t rung = 0; rung < 6; ++rung) {
        is_rung = is_rung || length == (std::size_t{1} << 40) >> (2 * rung);
    }
    CRUCIBLE_TEST_ASSERT(test, is_rung);
    CRUCIBLE_TEST_ASSERT(test, window.record().region_length() == window.layout().region_bytes());
    pass(test, "  ladder_with_no_cap: PASSED\n");
}

void new_map_reads_no_owner(TestRunnerCtx const& test) {
    alloc::Window window = reserve_capped(4 * kGibibyte);
    alloc::WindowRecord const record = window.record();
    std::size_t owned = 0;
    for (std::uint32_t chunk = 0; chunk < record.chunk_count(); ++chunk) {
        alloc::AddressWord const address = record.address_of(alloc::ChunkIndex{chunk});
        if (record.chunk_map().owner_of(address) != alloc::ChunkOwner::none) ++owned;
    }
    CRUCIBLE_TEST_ASSERT(test, owned == 0);
    // A write of the entry of the last chunk reads back from each byte of
    // the chunk, and the entry of the chunk below stays empty.
    alloc::ChunkIndex const last{static_cast<std::uint32_t>(record.chunk_count() - 1)};
    alloc::ChunkOwner const owner = alloc::owner_entry_of(alloc::HeapId{9});
    window.set_owner(last, owner);
    std::uint64_t const last_byte = static_cast<std::uint64_t>(record.address_of(last)) + alloc::kChunkBytes - 1;
    CRUCIBLE_TEST_ASSERT(test, record.chunk_map().owner_of(record.address_of(last)) == owner);
    CRUCIBLE_TEST_ASSERT(test, record.chunk_map().owner_of(alloc::AddressWord{last_byte}) == owner);
    CRUCIBLE_TEST_ASSERT(test, record.chunk_of(alloc::AddressWord{last_byte}) == last);
    alloc::ChunkIndex const below{last.raw() - 1};
    CRUCIBLE_TEST_ASSERT(test, record.chunk_map().owner_of(record.address_of(below)) == alloc::ChunkOwner::none);
    // The checked read refuses the first byte past the window and the byte
    // below its base.
    std::uint64_t const base = static_cast<std::uint64_t>(record.base());
    CRUCIBLE_TEST_ASSERT(test, record.checked_owner_of(alloc::AddressWord{last_byte}).value_or(alloc::ChunkOwner::none)
                                   == owner);
    CRUCIBLE_TEST_ASSERT(test, record.checked_owner_of(alloc::AddressWord{last_byte + 1}).is_none());
    CRUCIBLE_TEST_ASSERT(test, record.checked_owner_of(alloc::AddressWord{base - 1}).is_none());
    pass(test, "  new_map_reads_no_owner: PASSED\n");
}

// THE TAG BYTE of foundation/alloc/ChunkMap.h: on aarch64 an address with a
// tag in its top byte reads the chunk and the owner of the same address with
// no tag.  On x86-64 the same word lies outside the window, and the checked
// read refuses it.
void tagged_address_reads_its_chunk(TestRunnerCtx const& test) {
    alloc::Window window = reserve_capped(4 * kGibibyte);
    alloc::WindowRecord const record = window.record();
    alloc::ChunkIndex const chunk{7};
    alloc::ChunkOwner const owner = alloc::owner_entry_of(alloc::HeapId{5});
    window.set_owner(chunk, owner);
    std::uint64_t const address = static_cast<std::uint64_t>(record.address_of(chunk)) + 4096;
    alloc::AddressWord const tagged{address | (std::uint64_t{0x5A} << 56)};
    CRUCIBLE_TEST_ASSERT(test, alloc::strip_tag(alloc::AddressWord{address}) == alloc::AddressWord{address});
#if defined(__aarch64__)
    CRUCIBLE_TEST_ASSERT(test, record.is_in_window(tagged));
    CRUCIBLE_TEST_ASSERT(test, record.chunk_of(tagged) == chunk);
    CRUCIBLE_TEST_ASSERT(test, record.chunk_map().owner_of(tagged) == owner);
    CRUCIBLE_TEST_ASSERT(test, record.checked_owner_of(tagged).value_or(alloc::ChunkOwner::none) == owner);
    CRUCIBLE_TEST_ASSERT(test, alloc::strip_tag(tagged) == alloc::AddressWord{address});
#else
    CRUCIBLE_TEST_ASSERT(test, !record.is_in_window(tagged));
    CRUCIBLE_TEST_ASSERT(test, record.checked_owner_of(tagged).is_none());
#endif
    pass(test, "  tagged_address_reads_its_chunk: PASSED\n");
}

void records_inside_the_region(TestRunnerCtx const& test) {
    alloc::Window window = reserve_capped(4 * kGibibyte);
    alloc::WindowRecord const record = window.record();
    alloc::MetadataLayout const layout = window.layout();
    alloc::ChunkIndex const first{0};
    alloc::ChunkIndex const last{static_cast<std::uint32_t>(record.chunk_count() - 1)};
    alloc::UnitIndex const last_unit{::foundation::core::StaticIndex<alloc::kUnitsPerChunk - 1>{}};
    std::size_t const region = record.region_length().raw();
    std::size_t const guard = alloc::kMetadataGuardBytes;
    CRUCIBLE_TEST_ASSERT(test, layout.chunk_meta_offset(first).raw() >= guard);
    CRUCIBLE_TEST_ASSERT(test, layout.chunk_meta_offset(last).raw() + alloc::kChunkMetaBytes <= region - guard);
    std::size_t const last_slab_end = layout.slab_meta_offset(last, last_unit).raw() + alloc::kSlabMetaBytes;
    CRUCIBLE_TEST_ASSERT(test, last_slab_end <= region - guard);
    alloc::WindowCommits commits{};
    auto const first_meta = window.start_chunk_meta(commits, first).expect("a commit of the step of the record");
    auto const last_meta = window.start_chunk_meta(commits, last).expect("a commit of the step of the record");
    auto const last_slab =
        window.start_slab_meta(commits, last, last_unit).expect("a commit of the step of the record");
    CRUCIBLE_TEST_ASSERT(test, first_meta->owner() == alloc::ChunkOwner::none);
    CRUCIBLE_TEST_ASSERT(test, last_meta->state() == alloc::ChunkState::clean);
    CRUCIBLE_TEST_ASSERT(test, last_meta->source_word() == alloc::SourceWord{});
    CRUCIBLE_TEST_ASSERT(test, last_meta->mark_stamp() == alloc::MarkStamp{});
    CRUCIBLE_TEST_ASSERT(test, last_slab->free_count().raw() == 0);
    CRUCIBLE_TEST_ASSERT(test, last_slab->summary() == alloc::SummaryBits{});
    pass(test, "  records_inside_the_region: PASSED\n");
}

void guard_chunk_faults(TestRunnerCtx const& test) {
    alloc::Window window = reserve_capped(4 * kGibibyte);
    alloc::WindowRecord const record = window.record();
    alloc::ChunkIndex const below_guard{alloc::kChunksPerExtent - 2};
    alloc::ChunkIndex const guard{alloc::kChunksPerExtent - 1};
    alloc::WindowCommits commits{};
    CRUCIBLE_TEST_ASSERT(test, window.commit_extent_of(commits, below_guard).is_ok());
    CRUCIBLE_TEST_ASSERT(test, window.is_extent_committed(commits, guard));
    CRUCIBLE_TEST_ASSERT(test, read_first_byte(record, below_guard) == 0);
    expect_dies_by(
        test, [record, guard] noexcept { static_cast<void>(read_first_byte(record, guard)); },
        FatalSignal::segmentation_fault);
    pass(test, "  guard_chunk_faults: PASSED\n");
}

void mappings_follow_extents(TestRunnerCtx const& test) {
    alloc::Window window = reserve_any();
    alloc::WindowRecord const record = window.record();
    alloc::UnitIndex const first_unit{::foundation::core::StaticIndex<0>{}};
    constexpr std::size_t kChunks = 4096;
    CRUCIBLE_TEST_ASSERT(test, record.chunk_count() >= kChunks + kChunks / alloc::kChunksPerExtent + 1);
    std::size_t const before = count_window_mappings(test, record);
    alloc::WindowCommits commits{};
    std::size_t after_first = 0;
    std::size_t taken = 0;
    std::uint32_t extents = 0;
    for (std::uint32_t index = 0; taken < kChunks; ++index) {
        alloc::ChunkIndex const chunk{index};
        if (alloc::is_guard_chunk(chunk)) continue;
        if (!window.is_extent_committed(commits, chunk)) ++extents;
        CRUCIBLE_TEST_ASSERT(test, window.commit_extent_of(commits, chunk).is_ok());
        CRUCIBLE_TEST_ASSERT(test, window.start_chunk_meta(commits, chunk).is_ok());
        CRUCIBLE_TEST_ASSERT(test, window.start_slab_meta(commits, chunk, first_unit).is_ok());
        window.set_owner(chunk, alloc::owner_entry_of(alloc::HeapId{1}));
        ++taken;
        if (taken == 1) after_first = count_window_mappings(test, record);
        if (taken == alloc::kChunksPerExtent - 1) {
            std::size_t const in_extent = count_window_mappings(test, record);
            CRUCIBLE_TEST_ASSERT(test, kIsUnderEmulator || in_extent == after_first);
        }
    }
    std::size_t const after = count_window_mappings(test, record);
    std::size_t const sections = 2;
    CRUCIBLE_TEST_ASSERT(test, extents == 9);
    if (kIsUnderEmulator) {
        ::foundation::core::report(::foundation::core::Sink::Err,
                                   "mappings_follow_extents: the two counts of the lines SKIPPED (qemu-user)\n");
    } else {
        CRUCIBLE_TEST_ASSERT(test, after <= before + 2 * extents + 2 * sections);
    }
    pass(test, "  mappings_follow_extents: PASSED\n");
}

void host_slot_only(TestRunnerCtx const& test) {
    alloc::Window const window = reserve_capped(4 * kGibibyte);
    alloc::WindowTable table{};
    alloc::publish_host_window(window, table);
    CRUCIBLE_TEST_ASSERT(test, table.host().base() == window.record().base());
    CRUCIBLE_TEST_ASSERT(test, table.host().chunk_map() == window.record().chunk_map());
    bool is_other_empty = true;
    for (std::size_t slot = 1; slot < alloc::kWindowSlots; ++slot) {
        is_other_empty = is_other_empty && table.record(alloc::WindowSlot::check(slot).expect("a slot")).is_empty();
    }
    CRUCIBLE_TEST_ASSERT(test, is_other_empty);
    pass(test, "  host_slot_only: PASSED\n");
}

// The 63 slots of the devices take records in order from slot 1, and the
// 64th device gets table_full with no end of the process.  The table does not
// compare records, so one window stands for each device here.
void device_slots_fill_then_refuse(TestRunnerCtx const& test) {
    alloc::Window const window = reserve_capped(4 * kGibibyte);
    alloc::WindowTable table{};
    alloc::publish_host_window(window, table);
    bool is_in_order = true;
    for (std::size_t device = 1; device < alloc::kWindowSlots; ++device) {
        alloc::WindowSlot const expected = alloc::WindowSlot::check(device).expect("a slot below kWindowSlots");
        bool const is_expected = alloc::publish_device_window(window.record(), table)
                                     .match([expected](alloc::WindowSlot slot) noexcept { return slot == expected; },
                                            [](alloc::WindowTableError) noexcept { return false; });
        is_in_order = is_in_order && is_expected;
    }
    CRUCIBLE_TEST_ASSERT(test, is_in_order);
    bool const is_full =
        alloc::publish_device_window(window.record(), table)
            .match([](alloc::WindowSlot) noexcept { return false; },
                   [](alloc::WindowTableError error) noexcept { return error == alloc::WindowTableError::table_full; });
    CRUCIBLE_TEST_ASSERT(test, is_full);
    CRUCIBLE_TEST_ASSERT(test, table.host().base() == window.record().base());
    pass(test, "  device_slots_fill_then_refuse: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    ladder_under_a_cap(test);
    full_window_gives_the_remedy(test);
    ladder_with_no_cap(test);
    new_map_reads_no_owner(test);
    tagged_address_reads_its_chunk(test);
    records_inside_the_region(test);
    guard_chunk_faults(test);
    mappings_follow_extents(test);
    host_slot_only(test);
    device_slots_fill_then_refuse(test);
    pass(test, "test_alloc_chunk_map: PASSED\n");
    return 0;
}
