// The bodies of the window door of the allocator of the base
// (foundation/alloc/Window.h).  Each call of the kernel goes through the page
// source of foundation/os/PageSource.h, so this file includes no system
// header.

#include <foundation/alloc/Window.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Format.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Side.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace foundation::alloc {

namespace detail {

struct WindowRecordDoor {
    // The biased map of a window whose map starts MAP_OFFSET bytes after the
    // first byte of the region.  The sum wraps for a window above the map,
    // and the read of an entry of the window wraps back (THE MAP READ).
    [[nodiscard]] static BiasedChunkMap map_of(RegionBase region, core::ByteCount map_offset,
                                               WindowBase window) noexcept {
        std::uint64_t const map = static_cast<std::uint64_t>(region.value()) + map_offset.raw();
        std::uint64_t const window_chunks = static_cast<std::uint64_t>(window.value()) >> kChunkShift;
        return BiasedChunkMap{map - (window_chunks << 1)};
    }

    [[nodiscard]] static WindowRecord record_of(WindowBase base, WindowLength length, BiasedChunkMap chunk_map,
                                                RegionBase region_base, RegionLength region_length) noexcept {
        return WindowRecord{base, length, chunk_map, region_base, region_length};
    }

    [[nodiscard]] static Window window_of(os::Reservation&& window, os::Reservation&& region, MetadataLayout layout,
                                          WindowRecord record) noexcept {
        return Window{std::move(window), std::move(region), layout, record};
    }

    static void place(WindowTable& table, WindowSlot slot, WindowRecord record) noexcept {
        table.records_[slot.raw()] = record;
    }

    // The text of the diagnostic, which a call empties first and a refusal
    // writes (THE FULL WINDOW).
    [[nodiscard]] static core::FixedText<kWindowDiagnosticBytes>& text_of(WindowDiagnostic& diagnostic) noexcept {
        return diagnostic.text_;
    }
};

}  // namespace detail

namespace {

// THE LADDER: rung R reserves 1 TiB >> (2 * R), from 1 TiB to 1 GiB.  A build
// with ThreadSanitizer starts at 64 GiB.
constexpr std::size_t kRungCount = 6;
#if defined(__SANITIZE_THREAD__)
constexpr std::size_t kFirstRung = 2;
#else
constexpr std::size_t kFirstRung = 0;
#endif

// A rung of THE LADDER.
using Rung = core::Index<kRungCount>;

[[nodiscard]] constexpr core::ByteCount rung_bytes(Rung rung) noexcept {
    return core::ByteCount{(std::size_t{1} << 40) >> (2 * rung.raw())};
}

[[nodiscard]] os::RangeStart start_at(core::ByteCount offset) noexcept {
    return core::side_as<os::side::RangeStart>(offset);
}

// Commits the part of RESERVATION of BYTES bytes at START.  Each part that
// this file asks for is inside the reservation and a whole count of pages.
[[nodiscard]] core::Result<core::Unit, os::Errno> commit_part(os::Reservation& reservation, os::RangeStart start,
                                                              core::ByteCount bytes) noexcept {
    os::ReservedRange const range =
        reservation.range(start, bytes).expect("the window door asks for a whole part inside its reservation");
    CRUCIBLE_TRY(committed, reservation.commit(range));
    static_cast<void>(committed);
    return core::Unit{};
}

// The window and the region of one rung, or the refusal of the kernel.
[[nodiscard]] core::Result<Window, os::Errno> reserve_rung(core::ByteCount window_bytes, os::ReserveCap cap) noexcept {
    MetadataLayout const layout = MetadataLayout::of(core::ElementCount{window_bytes.raw() >> kChunkShift});
    CRUCIBLE_TRY(window, os::reserve(window_bytes, os::ReserveAlignment::huge_page, cap));
    CRUCIBLE_TRY(region, os::reserve(layout.region_bytes(), os::ReserveAlignment::huge_page, cap));
    // The chunk map commits whole, so each read of an entry reads a page.
    CRUCIBLE_TRY(map_committed, commit_part(region, start_at(layout.map_offset()), layout.map_bytes()));
    static_cast<void>(map_committed);
    WindowBase const window_base = core::side_as<side::WindowBase>(AddressWord{window.address_of(window.whole())});
    RegionBase const region_base = core::side_as<side::RegionBase>(AddressWord{region.address_of(region.whole())});
    WindowRecord const record = detail::WindowRecordDoor::record_of(
        window_base, core::side_as<side::WindowLength>(window_bytes),
        detail::WindowRecordDoor::map_of(region_base, layout.map_offset(), window_base), region_base,
        core::side_as<side::RegionLength>(layout.region_bytes()));
    return detail::WindowRecordDoor::window_of(std::move(window), std::move(region), layout, record);
}

// Writes the remedy of a refusal of each rung into the diagnostic (THE FULL
// WINDOW).  A text past the capacity keeps its first characters.
CRUCIBLE_COLD void write_full_window(WindowDiagnostic& diagnostic, os::Errno refusal) noexcept {
    Rung const first = Rung::check(kFirstRung).expect("the first rung is below kRungCount");
    static_cast<void>(core::format(detail::WindowRecordDoor::text_of(diagnostic),
                                   "foundation: the window door got no window of the allocator. The page source "
                                   "refused each size of the ladder from {} GiB to 1 GiB of address space, the last "
                                   "with {}. Raise the address-space limit of the process (ulimit -v) or "
                                   "vm.max_map_count, or end other mappings of the process, and start it again.",
                                   rung_bytes(first).raw() >> 30, core::enum_name_text(refusal)));
}

}  // namespace

namespace detail {

core::Result<Window, WindowFull> reserve_window_(os::ReserveCap cap, WindowDiagnostic& diagnostic) noexcept {
    WindowRecordDoor::text_of(diagnostic) = core::FixedText<kWindowDiagnosticBytes>{};
    os::Errno refusal = os::Errno::enomem;
    for (std::size_t raw = kFirstRung; raw < kRungCount; ++raw) {
        Rung const rung = Rung::check(raw).expect("each rung of the loop is below kRungCount");
        core::Result<Window, os::Errno> taken = reserve_rung(rung_bytes(rung), cap);
        if (taken.is_ok()) {
            return std::move(taken).map_err([](os::Errno) noexcept { return WindowFull::no_address_space; });
        }
        refusal = taken.err().value_or(os::Errno::enomem);
    }
    write_full_window(diagnostic, refusal);
    return core::err(WindowFull::no_address_space);
}

}  // namespace detail

Window::Window(os::Reservation&& window, os::Reservation&& region, MetadataLayout layout, WindowRecord record) noexcept
    : window_{std::move(window)}, region_{std::move(region)}, layout_{layout}, record_{record} {}

core::Option<ChunkIndex> Window::claim_chunk(WindowCommits& commits) const noexcept {
    std::uint64_t const count = record_.chunk_count();
    std::uint32_t seen = commits.next_chunk_.load_acquire();
    for (;;) {
        std::uint32_t chunk = seen;
        if (is_guard_chunk(ChunkIndex{chunk})) ++chunk;
        if (chunk >= count) return core::none;
        core::Result<core::Unit, core::CasRefusal<std::uint32_t>> const moved =
            commits.next_chunk_.cas_acq_rel(seen, chunk + 1);
        if (moved.is_ok()) return core::Option<ChunkIndex>::some(ChunkIndex{chunk});
        seen = moved.err().expect("a refused compare-and-swap holds the word that it read").observed;
    }
}

bool Window::is_extent_committed(WindowCommits const& commits, ChunkIndex chunk) const noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    std::size_t const extent = chunk.raw() >> (kExtentShift - kChunkShift);
    std::uint64_t const word =
        commits.committed_extents_.get(extent / 64).expect("a window holds at most 1 TiB of extents")->load_acquire();
    return ((word >> (extent % 64)) & 1U) != 0;
}

core::Result<core::Unit, os::Errno> Window::commit_extent_of(WindowCommits& commits, ChunkIndex chunk) noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    CRUCIBLE_FATAL_INVARIANT(!is_guard_chunk(chunk));
    if (is_extent_committed(commits, chunk)) return core::Unit{};
    std::size_t const extent = chunk.raw() >> (kExtentShift - kChunkShift);
    CRUCIBLE_TRY(committed, commit_part(window_, start_at(core::ByteCount{extent << kExtentShift}),
                                        core::ByteCount{kExtentBytes - kChunkBytes}));
    static_cast<void>(committed);
    // Each word of the extent map is an Atomic, so the heaps of two threads
    // set their bits in one word at the same time.  The element door writes
    // no state of the array itself.
    std::uint64_t const bit = std::uint64_t{1} << (extent % 64);
    commits.committed_extents_.with_element(
        core::Index<decltype(commits.committed_extents_)::size()>::check(extent / 64)
            .expect("a window holds at most 1 TiB of extents"),
        [bit](core::Atomic<std::uint64_t>& word) noexcept { static_cast<void>(word.fetch_or_acq_rel(bit)); });
    return core::Unit{};
}

void Window::set_owner(ChunkIndex chunk, ChunkOwner owner) noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    std::uint64_t const map = static_cast<std::uint64_t>(record_.region_base()) + layout_.map_offset().raw();
    std::uint64_t const entry = map + std::uint64_t{chunk.raw()} * sizeof(ChunkOwner);
    *__builtin_bit_cast(ChunkOwner*, entry) = owner;
}

core::Result<core::Unit, os::Errno> Window::grow_section_(WindowCommits& commits, RecordSection section,
                                                          core::ByteCount end) noexcept {
    bool const is_chunk_section = section == RecordSection::chunk_records;
    core::Atomic<std::size_t>& front = is_chunk_section ? commits.chunk_records_front_ : commits.slab_records_front_;
    std::size_t const seen = front.load_acquire();
    if (end.raw() <= seen) return core::Unit{};
    std::size_t const section_offset =
        is_chunk_section ? layout_.chunk_records_offset().raw() : layout_.slab_records_offset().raw();
    std::size_t const step_end = (end.raw() + (kMetadataStepBytes - 1)) & ~(kMetadataStepBytes - 1);
    CRUCIBLE_TRY(committed, commit_part(region_, start_at(core::ByteCount{section_offset + seen}),
                                        core::ByteCount{step_end - seen}));
    static_cast<void>(committed);
    static_cast<void>(front.fetch_max_acq_rel(step_end));
    return core::Unit{};
}

core::Result<core::Unit, os::Errno> Window::commit_slab_records_of(WindowCommits& commits, ChunkIndex chunk) noexcept {
    UnitIndex const last{core::StaticIndex<kUnitsPerChunk - 1>{}};
    std::size_t const offset = layout_.slab_meta_offset(chunk, last).raw();
    std::size_t const in_section = offset - layout_.slab_records_offset().raw();
    return grow_section_(commits, RecordSection::slab_records, core::ByteCount{in_section + kSlabMetaBytes});
}

core::Result<ChunkMeta*, os::Errno> Window::start_chunk_meta_(WindowCommits& commits, ChunkIndex chunk) noexcept {
    std::size_t const offset = layout_.chunk_meta_offset(chunk).raw();
    std::size_t const in_section = offset - layout_.chunk_records_offset().raw();
    CRUCIBLE_TRY(grown,
                 grow_section_(commits, RecordSection::chunk_records, core::ByteCount{in_section + kChunkMetaBytes}));
    static_cast<void>(grown);
    void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(record_.region_base()) + offset);
    return ::new(place) ChunkMeta{};
}

core::Result<SlabMeta*, os::Errno> Window::start_slab_meta_(WindowCommits& commits, ChunkIndex chunk,
                                                            UnitIndex unit) noexcept {
    std::size_t const offset = layout_.slab_meta_offset(chunk, unit).raw();
    std::size_t const in_section = offset - layout_.slab_records_offset().raw();
    CRUCIBLE_TRY(grown,
                 grow_section_(commits, RecordSection::slab_records, core::ByteCount{in_section + kSlabMetaBytes}));
    static_cast<void>(grown);
    void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(record_.region_base()) + offset);
    return ::new(place) SlabMeta{};
}

void publish_host_window(Window const& window, WindowTable& table) noexcept {
    CRUCIBLE_FATAL_INVARIANT(table.host().is_empty());
    detail::WindowRecordDoor::place(table, kHostWindow, window.record());
}

core::Result<WindowSlot, WindowTableError> publish_device_window(WindowRecord record, WindowTable& table) noexcept {
    CRUCIBLE_FATAL_INVARIANT(!record.is_empty());
    for (WindowSlot const slot : core::IndexRange<kWindowSlots>{}) {
        if (slot == kHostWindow || !table.record(slot).is_empty()) continue;
        detail::WindowRecordDoor::place(table, slot, record);
        return slot;
    }
    return core::err(WindowTableError::table_full);
}

}  // namespace foundation::alloc
