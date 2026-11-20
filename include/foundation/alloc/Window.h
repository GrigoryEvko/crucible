#pragma once

// The window door of the allocator of the base: the reservation of a window
// and of its metadata region, the ladder of sizes, and the commits of an
// extent and of the records (foundation/alloc/ChunkMap.h, THE WINDOW).
//
//   reserve_window(diagnostic)       the window of the first size of the
//                                    ladder that the kernel gives, or
//                                    WindowFull with the remedy in the
//                                    diagnostic (THE FULL WINDOW)
//   reserve_window(cap, diagnostic)  the same, with each reserve above cap
//                                    refused with no call (the test door of
//                                    THE LADDER)
//   window.record()                  the WindowRecord of the window
//   window.layout()                  the MetadataLayout of its region
//   window.commit_extent_of(chunk)   commits the extent of the chunk, one
//                                    time (THE COMMITS)
//   window.start_chunk_meta(chunk)   the ChunkMeta of the chunk, in zero
//                                    bytes, after the commit of its step
//   window.start_slab_meta(chunk, unit)
//                                    the SlabMeta of the unit, the same way
//   publish_host_window(window, table)
//                                    writes the record of the window into the
//                                    host slot of the table
//   publish_device_window(record, table)
//                                    writes the record of the window of a
//                                    device into the first empty slot of a
//                                    device, or gives table_full
//
// THE LADDER
//   The sizes are 1 TiB, 256 GiB, 64 GiB, 16 GiB, 4 GiB and 1 GiB.  A build
//   with ThreadSanitizer starts at 64 GiB.  For each size, the door reserves
//   the window at the alignment of a chunk, and then its region.  When the
//   kernel refuses one of the two, the door unmaps the other and takes the
//   next size.  When the kernel refuses each size, the door gives
//   WindowFull (THE FULL WINDOW).
//
// THE FULL WINDOW
//   The window is the address-space limit of the allocator, as the memory of
//   the host is the limit of its heap.  When the kernel refuses each size of
//   THE LADDER, the door gives WindowFull::no_address_space, and it writes
//   the remedy into the WindowDiagnostic of the caller: the sizes that the
//   page source refused, the name of the error of the last refusal, and the
//   limits of the process to raise.  The Result holds only the small kind,
//   so the error never allocates, and the caller keeps expect and
//   CRUCIBLE_TRY.  Each call empties the diagnostic first, so a call that
//   succeeds leaves it empty, and no text stays in a state of the thread.
//
// THE COMMITS
//   Each commit is one mprotect of a range that is next to a range with the
//   same access, so the count of mappings of the process grows with the
//   count of extents and sections, and not with the count of chunks.  The
//   kernel refuses a new mapping past vm.max_map_count, also with memory
//   free.
//     The chunk map   committed whole at the reserve, so each read of the
//                     map reads a page, and each entry reads zero
//     An extent       committed whole, less its guard chunk, at the first
//                     commit of one of its chunks.  Its pages then commit at
//                     their first touch
//     A record        its section commits from its first byte up to the
//                     record, in whole steps of kMetadataStepBytes, adjacent
//                     to the last commit of the section
//   A record starts in zero bytes: start_chunk_meta and start_slab_meta
//   construct the record over those bytes and give a Ref to it.  Each call
//   constructs it again, so only the thread that takes the chunk or the unit
//   calls it, one time.
//
// One thread owns a Window: the init of the root, or the test that reserves
// it.  The door issues each call through the page source of
// foundation/os/PageSource.h.  This header is in the layer F0_doors of
// utils/rule-table.txt, and src/foundation/alloc/Window.cpp holds the
// bodies.

#include <foundation/Brand.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

// The words of the map of the committed extents of a window: one bit for
// each extent of a window of 1 TiB.
inline constexpr std::size_t kExtentMapWords = ((std::size_t{1} << 40) >> kExtentShift) / 64;

// The reason that the window door gives no room (THE FULL WINDOW).
enum class WindowFull : std::uint8_t {
    // The page source refused each size of THE LADDER: the process has no
    // free range of address space for the smallest window and its region.
    no_address_space,
};

// The largest count of the characters of the text of a WindowDiagnostic.
inline constexpr std::size_t kWindowDiagnosticBytes = 384;

// The remedy of the last refusal of the window door (THE FULL WINDOW).
class WindowDiagnostic final {
public:
    // The empty diagnostic.
    WindowDiagnostic() noexcept = default;

    // The text of the last refusal, or the empty text after a call that
    // succeeded.
    [[nodiscard]] ::foundation::core::TextView text() const& noexcept { return text_.view(); }
    ::foundation::core::TextView
    text() const&& = delete("the text of a temporary diagnostic points into an object that the full expression ends");

private:
    friend struct detail::WindowRecordDoor;

    ::foundation::core::FixedText<kWindowDiagnosticBytes> text_{};
};

// One window and its metadata region (THE WINDOW).  The window owns the two
// reservations, and its end unmaps them.
class [[nodiscard]] Window final {
public:
    Window(Window&&) noexcept = default;
    Window(Window const&) = delete("a window owns its reservations, and two owners would unmap them two times");
    Window& operator=(Window const&) = delete("a window owns its reservations for its life");
    Window& operator=(Window&&) = delete("a window owns its reservations for its life");
    ~Window() = default;

    [[nodiscard]] WindowRecord record() const noexcept { return record_; }
    [[nodiscard]] MetadataLayout layout() const noexcept { return layout_; }

    // True when the extent of the chunk is committed.
    [[nodiscard]] bool is_extent_committed(ChunkIndex chunk) const noexcept;

    // Commits the extent of the chunk, less its guard chunk, when it is not
    // committed (THE COMMITS).  The chunk is not a guard chunk.  The error is
    // the refusal of the kernel, such as enomem under strict overcommit.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    commit_extent_of(ChunkIndex chunk) noexcept;

    // Writes the entry of a chunk in the chunk map.  Only the thread that
    // takes or gives back the chunk calls it (THE MAP READ of
    // foundation/alloc/ChunkMap.h).  Complexity: O(1), one store.
    void set_owner(ChunkIndex chunk, ChunkOwner owner) noexcept;

    // The record of a chunk, constructed in zero bytes after the commit of
    // its step (THE COMMITS).  The Ref has the fresh brand of the call, and
    // the pack refuses each explicit template argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Result<
        ::foundation::core::Ref<ChunkMeta, ::foundation::brand::undeducible_brand_t<Fresh>>, ::foundation::os::Errno>
    start_chunk_meta(ChunkIndex chunk) noexcept {
        CRUCIBLE_TRY(meta, start_chunk_meta_(chunk));
        return ::foundation::core::detail::RefDoor::lend<::foundation::brand::undeducible_brand_t<Fresh>>(*meta);
    }

    // The record of a unit of a chunk, the same way.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Result<
        ::foundation::core::Ref<SlabMeta, ::foundation::brand::undeducible_brand_t<Fresh>>, ::foundation::os::Errno>
    start_slab_meta(ChunkIndex chunk, UnitIndex unit) noexcept {
        CRUCIBLE_TRY(meta, start_slab_meta_(chunk, unit));
        return ::foundation::core::detail::RefDoor::lend<::foundation::brand::undeducible_brand_t<Fresh>>(*meta);
    }

private:
    friend struct detail::WindowRecordDoor;

    // The cold bodies of the two starts: the commit of the step and the
    // construction of the record.  Each gives the address of the record.
    [[nodiscard]] ::foundation::core::Result<ChunkMeta*, ::foundation::os::Errno>
    start_chunk_meta_(ChunkIndex chunk) noexcept;
    [[nodiscard]] ::foundation::core::Result<SlabMeta*, ::foundation::os::Errno>
    start_slab_meta_(ChunkIndex chunk, UnitIndex unit) noexcept;

    Window(::foundation::os::Reservation&& window, ::foundation::os::Reservation&& region, MetadataLayout layout,
           WindowRecord record) noexcept;

    // The two sections of records, which grow at their own fronts.
    enum class RecordSection : std::uint8_t {
        chunk_records,
        slab_records,
    };

    // Commits the bytes of a section from its front up to END, a count of
    // bytes from the first byte of the section (THE COMMITS).
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    grow_section_(RecordSection section, ::foundation::core::ByteCount end) noexcept;

    ::foundation::os::Reservation window_;
    ::foundation::os::Reservation region_;
    MetadataLayout layout_;
    WindowRecord record_;
    ::foundation::core::FixedArray<std::uint64_t, kExtentMapWords> committed_extents_{};
    std::size_t chunk_records_front_ = 0;
    std::size_t slab_records_front_ = 0;
};

namespace detail {

// The cold body of the two doors below: the climb of THE LADDER.  On a
// refusal it writes the remedy into the diagnostic.
[[nodiscard]] ::foundation::core::Result<Window, WindowFull> reserve_window_(::foundation::os::ReserveCap cap,
                                                                             WindowDiagnostic& diagnostic) noexcept;

}  // namespace detail

// The window of the first size of THE LADDER that the kernel gives, or
// WindowFull with the remedy in the diagnostic (THE FULL WINDOW).
template <class Brand>
[[nodiscard]] ::foundation::core::Result<Window, WindowFull>
reserve_window(::foundation::core::Ref<WindowDiagnostic, Brand> diagnostic) noexcept {
    return detail::reserve_window_(::foundation::os::ReserveCap{~std::size_t{0}}, *diagnostic);
}

// The same, with each reserve above the cap refused with enomem and with no
// call: the test door of THE LADDER.
template <class Brand>
[[nodiscard]] ::foundation::core::Result<Window, WindowFull>
reserve_window(::foundation::os::ReserveCap cap, ::foundation::core::Ref<WindowDiagnostic, Brand> diagnostic) noexcept {
    return detail::reserve_window_(cap, *diagnostic);
}

// Writes the record of the window into the host slot of the table.  Only the
// init of the root calls it, one time, before it gives a block of the
// window.
void publish_host_window(Window const& window, WindowTable& table) noexcept;

// Writes the record of the window of a device into the first empty slot after
// the host slot, and gives that slot.  When each of the kWindowSlots - 1
// slots of a device holds a record, the door writes nothing and gives
// table_full.  The record is not empty.  Only the init of the window of a
// device calls it, one time for each device, and the caller holds the table
// alone.  Complexity: O(kWindowSlots).
[[nodiscard]] ::foundation::core::Result<WindowSlot, WindowTableError>
publish_device_window(WindowRecord record, WindowTable& table) noexcept;

}  // namespace foundation::alloc
