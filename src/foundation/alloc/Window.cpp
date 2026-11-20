// The bodies of the window door of the allocator of the base
// (foundation/alloc/Window.h).  Each call of the kernel goes through the page
// source of foundation/os/PageSource.h, so this file includes no system
// header.

#include <foundation/alloc/Window.h>

#include <foundation/Platform.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
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

// The window of the first rung that the kernel gives (THE LADDER).
[[nodiscard]] core::Result<Window, os::Errno> climb_ladder(os::ReserveCap cap) noexcept {
    os::Errno refusal = os::Errno::enomem;
    for (std::size_t raw = kFirstRung; raw < kRungCount; ++raw) {
        Rung const rung = Rung::check(raw).expect("each rung of the loop is below kRungCount");
        core::Result<Window, os::Errno> taken = reserve_rung(rung_bytes(rung), cap);
        if (taken.is_ok()) return taken;
        refusal = taken.err().value_or(os::Errno::enomem);
    }
    return core::err(refusal);
}

}  // namespace

Window::Window(os::Reservation&& window, os::Reservation&& region, MetadataLayout layout, WindowRecord record) noexcept
    : window_{std::move(window)}, region_{std::move(region)}, layout_{layout}, record_{record} {}

bool Window::is_extent_committed(ChunkIndex chunk) const noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    std::size_t const extent = chunk.raw() >> (kExtentShift - kChunkShift);
    std::uint64_t const word = *committed_extents_.get(extent / 64).expect("a window holds at most 1 TiB of extents");
    return ((word >> (extent % 64)) & 1U) != 0;
}

core::Result<core::Unit, os::Errno> Window::commit_extent_of(ChunkIndex chunk) noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    CRUCIBLE_FATAL_INVARIANT(!is_guard_chunk(chunk));
    if (is_extent_committed(chunk)) return core::Unit{};
    std::size_t const extent = chunk.raw() >> (kExtentShift - kChunkShift);
    CRUCIBLE_TRY(committed, commit_part(window_, start_at(core::ByteCount{extent << kExtentShift}),
                                        core::ByteCount{kExtentBytes - kChunkBytes}));
    static_cast<void>(committed);
    std::uint64_t& word = *committed_extents_.get(extent / 64).expect("a window holds at most 1 TiB of extents");
    word |= std::uint64_t{1} << (extent % 64);
    return core::Unit{};
}

void Window::set_owner(ChunkIndex chunk, ChunkOwner owner) noexcept {
    CRUCIBLE_FATAL_INVARIANT(chunk.raw() < record_.chunk_count());
    std::uint64_t const map = static_cast<std::uint64_t>(record_.region_base()) + layout_.map_offset().raw();
    std::uint64_t const entry = map + std::uint64_t{chunk.raw()} * sizeof(ChunkOwner);
    *__builtin_bit_cast(ChunkOwner*, entry) = owner;
}

core::Result<core::Unit, os::Errno> Window::grow_section_(RecordSection section, core::ByteCount end) noexcept {
    bool const is_chunk_section = section == RecordSection::chunk_records;
    std::size_t& front = is_chunk_section ? chunk_records_front_ : slab_records_front_;
    if (end.raw() <= front) return core::Unit{};
    std::size_t const section_offset =
        is_chunk_section ? layout_.chunk_records_offset().raw() : layout_.slab_records_offset().raw();
    std::size_t const step_end = (end.raw() + (kMetadataStepBytes - 1)) & ~(kMetadataStepBytes - 1);
    CRUCIBLE_TRY(committed, commit_part(region_, start_at(core::ByteCount{section_offset + front}),
                                        core::ByteCount{step_end - front}));
    static_cast<void>(committed);
    front = step_end;
    return core::Unit{};
}

core::Result<ChunkMeta*, os::Errno> Window::start_chunk_meta_(ChunkIndex chunk) noexcept {
    std::size_t const offset = layout_.chunk_meta_offset(chunk).raw();
    std::size_t const in_section = offset - layout_.chunk_records_offset().raw();
    CRUCIBLE_TRY(grown, grow_section_(RecordSection::chunk_records, core::ByteCount{in_section + kChunkMetaBytes}));
    static_cast<void>(grown);
    void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(record_.region_base()) + offset);
    return ::new(place) ChunkMeta{};
}

core::Result<SlabMeta*, os::Errno> Window::start_slab_meta_(ChunkIndex chunk, UnitIndex unit) noexcept {
    std::size_t const offset = layout_.slab_meta_offset(chunk, unit).raw();
    std::size_t const in_section = offset - layout_.slab_records_offset().raw();
    CRUCIBLE_TRY(grown, grow_section_(RecordSection::slab_records, core::ByteCount{in_section + kSlabMetaBytes}));
    static_cast<void>(grown);
    void* const place = __builtin_bit_cast(void*, static_cast<std::uint64_t>(record_.region_base()) + offset);
    return ::new(place) SlabMeta{};
}

core::Result<Window, os::Errno> reserve_window() noexcept { return climb_ladder(os::ReserveCap{~std::size_t{0}}); }

core::Result<Window, os::Errno> reserve_window(os::ReserveCap cap) noexcept { return climb_ladder(cap); }

void publish_host_window(Window const& window, WindowTable& table) noexcept {
    CRUCIBLE_FATAL_INVARIANT(table.host().is_empty());
    detail::WindowRecordDoor::place(table, kHostWindow, window.record());
}

}  // namespace foundation::alloc
