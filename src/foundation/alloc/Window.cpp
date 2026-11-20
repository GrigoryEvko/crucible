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
    [[nodiscard]] static BiasedChunkMap map_of(std::uint64_t word) noexcept { return BiasedChunkMap{word}; }

    [[nodiscard]] static WindowRecord record_of(std::uint64_t base, std::uint64_t length, BiasedChunkMap chunk_map,
                                                std::uint64_t region_base, std::uint64_t region_length) noexcept {
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

[[nodiscard]] constexpr std::size_t rung_bytes(std::size_t rung) noexcept {
    return (std::size_t{1} << 40) >> (2 * rung);
}

[[nodiscard]] os::RangeStart start_at(std::size_t offset) noexcept {
    return core::side_as<os::side::RangeStart>(core::ByteCount{offset});
}

// Commits the part of RESERVATION of BYTES bytes at OFFSET.  Each part that
// this file asks for is inside the reservation and a whole count of pages.
[[nodiscard]] core::Result<core::Unit, os::Errno> commit_part(os::Reservation& reservation, std::size_t offset,
                                                              core::ByteCount bytes) noexcept {
    os::ReservedRange const range = reservation.range(start_at(offset), bytes)
                                        .expect("the window door asks for a whole part inside its reservation");
    CRUCIBLE_TRY(committed, reservation.commit(range));
    static_cast<void>(committed);
    return core::Unit{};
}

// The window and the region of one rung, or the refusal of the kernel.
[[nodiscard]] core::Result<Window, os::Errno> reserve_rung(std::size_t window_bytes, os::ReserveCap cap) noexcept {
    MetadataLayout const layout = MetadataLayout::of(core::ElementCount{window_bytes >> kChunkShift});
    CRUCIBLE_TRY(window, os::reserve(core::ByteCount{window_bytes}, os::ReserveAlignment::huge_page, cap));
    CRUCIBLE_TRY(region, os::reserve(layout.region_bytes(), os::ReserveAlignment::huge_page, cap));
    // The chunk map commits whole, so each read of an entry reads a page.
    CRUCIBLE_TRY(map_committed, commit_part(region, layout.map_offset().raw(), layout.map_bytes()));
    static_cast<void>(map_committed);
    std::uint64_t const window_base = window.address_of(window.whole());
    std::uint64_t const region_base = region.address_of(region.whole());
    // The sum wraps for a window above the map, and the read of an entry of
    // the window wraps back (THE MAP READ).
    std::uint64_t const biased = region_base + layout.map_offset().raw() - ((window_base >> kChunkShift) << 1);
    WindowRecord const record = detail::WindowRecordDoor::record_of(
        window_base, window_bytes, detail::WindowRecordDoor::map_of(biased), region_base, layout.region_bytes().raw());
    return detail::WindowRecordDoor::window_of(std::move(window), std::move(region), layout, record);
}

// The window of the first rung that the kernel gives (THE LADDER).
[[nodiscard]] core::Result<Window, os::Errno> climb_ladder(os::ReserveCap cap) noexcept {
    os::Errno refusal = os::Errno::enomem;
    for (std::size_t rung = kFirstRung; rung < kRungCount; ++rung) {
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
    CRUCIBLE_TRY(committed, commit_part(window_, extent << kExtentShift, core::ByteCount{kExtentBytes - kChunkBytes}));
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
    CRUCIBLE_TRY(committed, commit_part(region_, section_offset + front, core::ByteCount{step_end - front}));
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
