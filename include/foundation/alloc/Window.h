#pragma once

// The window door of the allocator of the base: the reservation of a window
// and of its metadata region, the ladder of sizes, and the commits of an
// extent and of the records (foundation/alloc/ChunkMap.h, the window).
//
//   reserve_window(diagnostic)       the window of the first size of the
//                                    ladder that the kernel gives, or
//                                    WindowFull with the remedy in the
//                                    diagnostic (the full window)
//   reserve_window(cap, diagnostic)  the same, with each reserve above cap
//                                    refused with no call (the test door of
//                                    the ladder)
//   window.record()                  the WindowRecord of the window
//   window.layout()                  the MetadataLayout of its region
//   window.slab_records_at()         the address of the record of the slab
//                                    0, for a SlabRecords
//   window.set_owner(chunk, owner)   writes the entry of the chunk in the
//                                    chunk map
//   ChunkSource                      a window and the words of its commits
//                                    that many threads change: the bit of
//                                    each committed extent, the front of
//                                    each section of records, and the claim
//                                    index of chunks (the chunk source)
//   source.record(), source.layout(), source.slab_records_at()
//                                    the same of the window of the source
//   source.claim_chunk()             the next chunk that no thread claimed,
//                                    or none past the last chunk
//   source.claimed_chunks()          the chunks below the claim index
//   source.commit_slab_records_of(chunk)
//                                    commits the records of the 32 units of
//                                    the chunk, with no record started
//   source.commit_extent_of(chunk)   commits the extent of the chunk, one
//                                    time (the commits)
//   source.start_chunk_meta(chunk)   the ChunkMeta of the chunk, in zero
//                                    bytes, after the commit of its step
//   source.start_slab_meta(chunk, unit)
//                                    the SlabMeta of the unit, the same way
//   source.set_owner(chunk, owner)   window.set_owner of the window
//   publish_host_window(record, table)
//                                    writes the record of the host window into
//                                    the host slot of the table
//   publish_device_window(record, table)
//                                    writes the record of the window of a
//                                    device into the first empty slot of a
//                                    device, or gives table_full
//
// Ladder
//   The sizes are 1 TiB, 256 GiB, 64 GiB, 16 GiB, 4 GiB and 1 GiB.  A build
//   with ThreadSanitizer starts at 64 GiB.  For each size, the door reserves
//   the window at the alignment of a chunk, and then its region.  When the
//   kernel refuses one of the two, the door unmaps the other and takes the
//   next size.  When the kernel refuses each size, the door gives
//   WindowFull (the full window).
//
// Full window
//   The window is the address-space limit of the allocator, as the memory of
//   the host is the limit of its heap.  When the kernel refuses each size of
//   the ladder, the door gives WindowFull::no_address_space, and it writes
//   the remedy into the WindowDiagnostic of the caller: the sizes that the
//   page source refused, the name of the error of the last refusal, and the
//   limits of the process to raise.  The Result holds only the small kind,
//   so the error never allocates, and the caller keeps expect and
//   CRUCIBLE_TRY.  Each call empties the diagnostic first, so a call that
//   succeeds leaves it empty, and no text stays in a state of the thread.
//
// Commits
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
//   constructs it again, so only the thread that claimed the chunk calls it,
//   one time for the chunk and one time for each unit.
//
// Chunk source
//   Many heaps take chunks of one window at the same time, so each word that
//   a commit changes is an atomic word of the ChunkSource, and no step takes a
//   lock.  A claim moves the claim index past the next chunk that is not a
//   guard with one compare-and-swap, so each chunk has one claim, and a claim
//   past the last chunk gives none.  An extent commits with one mprotect and
//   then sets its bit with fetch_or.  A section of records commits from the
//   front that it reads up to the end of a step, and then raises the front
//   with fetch_max.  Two threads can commit one range at the same time: the
//   kernel gives it the same access two times, and the mappings stay merged.
//   Each bit and each front rises only after its commit, with release, and a
//   reader reads it with acquire, so each byte below a front that a thread
//   reads is committed.  The node lists of the NUMA extents and of the purge
//   replace the claim index: they give a chunk back to its node, and a later
//   claim takes it again.
//
//   An atomic word does not move, and a Window moves out of the door that
//   reserves it.  So a ChunkSource takes the Window by move and stays in its
//   place: the root of the allocator for the host window, and the test for a
//   window of a test.  Each door that changes a commit is a member of the
//   source, and no door gives the window out, so one window has one set of
//   words.  A second set of words would give one chunk two claims, and two
//   heaps would write it.
//
// The thread that reserves a Window owns the reservations: the init of the
// root, or a test.  The door issues each call through the page source of
// foundation/os/PageSource.h.  This header is in the layer F0_doors of
// utils/rule-table.txt, and src/foundation/alloc/Window.cpp holds the
// bodies.

#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

class ChunkSource;

}  // namespace foundation::alloc

namespace foundation::core {

// The door of the window of the allocator to the door of a Ref (the doors of
// the owners above foundation/core/Ref.h).  Its one member is private, and
// ChunkSource is its one friend.  The invariant: the source lends the record
// of a chunk or of a unit only after the commit of its step and the
// construction of the record, the record lives as long as the window, and the
// brand is the fresh brand of the call of the start.  A home owner row of
// utils/rule-table.txt keeps this definition in this header.
class AllocWindowRefDoor final : ::foundation::NoObject<AllocWindowRefDoor> {
    friend class ::foundation::alloc::ChunkSource;

    // A record that the source started, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

}  // namespace foundation::core

namespace foundation::alloc {

// The words of the map of the committed extents of a window: one bit for
// each extent of a window of 1 TiB.
inline constexpr std::size_t kExtentMapWords = ((std::size_t{1} << 40) >> kExtentShift) / 64;

// The reason that the window door gives no room (the full window).
enum class WindowFull : std::uint8_t {
    // The page source refused each size of the ladder: the process has no
    // free range of address space for the smallest window and its region.
    no_address_space,
};

// The largest count of the characters of the text of a WindowDiagnostic.
inline constexpr std::size_t kWindowDiagnosticBytes = 384;

// The remedy of the last refusal of the window door (the full window).
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

// One window and its metadata region (the window).  The window owns the two
// reservations, and its end unmaps them.  A ChunkSource takes it to give its
// chunks out.
class [[nodiscard]] Window final {
public:
    Window(Window&&) noexcept = default;
    Window(Window const&) = delete("a window owns its reservations, and two owners would unmap them two times");
    Window& operator=(Window const&) = delete("a window owns its reservations for its life");
    Window& operator=(Window&&) = delete("a window owns its reservations for its life");
    ~Window() = default;

    [[nodiscard]] WindowRecord record() const noexcept { return record_; }
    [[nodiscard]] MetadataLayout layout() const noexcept { return layout_; }

    // The address of the record of the slab 0 of the window: the first byte
    // of its section of slab records.  A SlabRecords of
    // foundation/alloc/SlabHeap.h holds it.  Complexity: O(1).
    [[nodiscard]] AddressWord slab_records_at() const noexcept {
        return AddressWord{static_cast<std::uint64_t>(record_.region_base()) + layout_.slab_records_offset().raw()};
    }

    // Writes the entry of a chunk in the chunk map.  Only the thread that
    // takes or gives back the chunk calls it (the map read of
    // foundation/alloc/ChunkMap.h).  Complexity: O(1), one store.
    void set_owner(ChunkIndex chunk, ChunkOwner owner) noexcept;

private:
    friend struct detail::WindowRecordDoor;
    friend class ChunkSource;

    Window(::foundation::os::Reservation&& window, ::foundation::os::Reservation&& region, MetadataLayout layout,
           WindowRecord record) noexcept;

    ::foundation::os::Reservation window_;
    ::foundation::os::Reservation region_;
    MetadataLayout layout_;
    WindowRecord record_;
};

// One window and the words of its commits that many threads change (the chunk
// source).  It takes the window that the reserve door gave, and no door gives
// the window out again, so one window has one set of words.  The words start
// at zero: no chunk claimed, no extent committed, and each section of records
// at its first byte.  The end of the source ends the window.
//
// The source states owns_its_storage{} of foundation/core/Owner.h.  It takes
// no pointer and no borrow of a caller, and each member keeps each page of
// the window and each record until the end of the source:
//   claim_chunk              moves the claim index, one word
//   commit_extent_of         gives pages read and write access
//   commit_slab_records_of   gives pages read and write access
//   start_chunk_meta, start_slab_meta
//                            give pages read and write access and construct
//                            one record
//   set_owner                writes one entry of the chunk map.  It moves and
//                            ends nothing, so a read of the map can see a
//                            different entry and never ended memory
// No member decommits, grows, moves or unmaps a page.
class[[= ::foundation::core::owns_its_storage{}]] ChunkSource final {
public:
    explicit ChunkSource(Window&& window) noexcept : window_{static_cast<Window&&>(window)} {}
    explicit ChunkSource(Window const&) = delete("a source takes its window by move, so one window has one set of "
                                                 "commit words: give the window that the reserve door gave");

    ChunkSource(ChunkSource const&) = delete("a source holds the one set of the commit words of its window");
    ChunkSource(ChunkSource&&) = delete("a source holds atomic words, which other threads reach at their address");
    ChunkSource& operator=(ChunkSource const&) = delete("a source holds one window for its life");
    ChunkSource& operator=(ChunkSource&&) = delete("a source holds one window for its life");
    ~ChunkSource() = default;

    // The record, the layout and the first slab record of the window.
    [[nodiscard]] WindowRecord record() const noexcept { return window_.record(); }
    [[nodiscard]] MetadataLayout layout() const noexcept { return window_.layout(); }
    [[nodiscard]] AddressWord slab_records_at() const noexcept { return window_.slab_records_at(); }

    // The next chunk that no claim took, past each guard chunk, or none when
    // each chunk of the window has a claim (the chunk source).  Complexity:
    // O(1) compare-and-swap steps for each thread that claims at the same
    // time.
    [[nodiscard]] ::foundation::core::Option<ChunkIndex> claim_chunk() noexcept;

    // The count of the chunks below the claim index: each chunk that a claim
    // took is below it, with the guard chunks between them.  One acquire
    // load.
    [[nodiscard]] ::foundation::core::ElementCount claimed_chunks() const noexcept {
        return ::foundation::core::ElementCount{std::size_t{next_chunk_.load_acquire()}};
    }

    // True when the extent of the chunk is committed.  One acquire load.
    [[nodiscard]] bool is_extent_committed(ChunkIndex chunk) const noexcept;

    // Commits the section of slab records up to the end of the records of
    // the 32 units of the chunk, with no record started (the commits).  A
    // heap calls it when it takes the chunk, so a read of the record of each
    // unit of the chunk reads a committed page, and a record that no slab
    // started reads zero.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    commit_slab_records_of(ChunkIndex chunk) noexcept;

    // Commits the extent of the chunk, less its guard chunk, when it is not
    // committed (the commits).  The chunk is not a guard chunk.  The error is
    // the refusal of the kernel, such as enomem under strict overcommit.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    commit_extent_of(ChunkIndex chunk) noexcept;

    // The set_owner of the window of the source.
    void set_owner(ChunkIndex chunk, ChunkOwner owner) noexcept { window_.set_owner(chunk, owner); }

    // The record of a chunk, constructed in zero bytes after the commit of
    // its step (the commits).  The Ref has the fresh brand of the call, and
    // the pack refuses each explicit template argument.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Result<
        ::foundation::core::Ref<ChunkMeta, ::foundation::brand::undeducible_brand_t<Fresh>>, ::foundation::os::Errno>
    start_chunk_meta(ChunkIndex chunk) noexcept {
        CRUCIBLE_TRY(meta, start_chunk_meta_(chunk));
        return ::foundation::core::AllocWindowRefDoor::lend_<::foundation::brand::undeducible_brand_t<Fresh>>(*meta);
    }

    // The record of a unit of a chunk, the same way.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Result<
        ::foundation::core::Ref<SlabMeta, ::foundation::brand::undeducible_brand_t<Fresh>>, ::foundation::os::Errno>
    start_slab_meta(ChunkIndex chunk, UnitIndex unit) noexcept {
        CRUCIBLE_TRY(meta, start_slab_meta_(chunk, unit));
        return ::foundation::core::AllocWindowRefDoor::lend_<::foundation::brand::undeducible_brand_t<Fresh>>(*meta);
    }

private:
    // The cold bodies of the two starts: the commit of the step and the
    // construction of the record.  Each gives the address of the record.
    [[nodiscard]] ::foundation::core::Result<ChunkMeta*, ::foundation::os::Errno>
    start_chunk_meta_(ChunkIndex chunk) noexcept;
    [[nodiscard]] ::foundation::core::Result<SlabMeta*, ::foundation::os::Errno>
    start_slab_meta_(ChunkIndex chunk, UnitIndex unit) noexcept;

    // The two sections of records, which grow at their own fronts.
    enum class RecordSection : std::uint8_t {
        chunk_records,
        slab_records,
    };

    // Commits the bytes of a section from its front up to end, a count of
    // bytes from the first byte of the section (the chunk source).
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    grow_section_(RecordSection section, ::foundation::core::ByteCount end) noexcept;

    Window window_;
    // One bit for each extent of a window of 1 TiB.
    ::foundation::core::FixedArray<::foundation::core::Atomic<std::uint64_t>, kExtentMapWords> committed_extents_{};
    // The committed bytes of each section of records, from its first byte.
    ::foundation::core::Atomic<std::size_t> chunk_records_front_{0};
    ::foundation::core::Atomic<std::size_t> slab_records_front_{0};
    // The index of the first chunk that no claim took.
    ::foundation::core::Atomic<std::uint32_t> next_chunk_{0};
};

namespace detail {

// The cold body of the two doors below: the climb of the ladder.  On a
// refusal it writes the remedy into the diagnostic.
[[nodiscard]] ::foundation::core::Result<Window, WindowFull> reserve_window_(::foundation::os::ReserveCap cap,
                                                                             WindowDiagnostic& diagnostic) noexcept;

}  // namespace detail

// The window of the first size of the ladder that the kernel gives, or
// WindowFull with the remedy in the diagnostic (the full window).
template <class Brand>
[[nodiscard]] ::foundation::core::Result<Window, WindowFull>
reserve_window(::foundation::core::Ref<WindowDiagnostic, Brand> diagnostic) noexcept {
    return detail::reserve_window_(::foundation::os::ReserveCap{~std::size_t{0}}, *diagnostic);
}

// The same, with each reserve above the cap refused with enomem and with no
// call: the test door of the ladder.
template <class Brand>
[[nodiscard]] ::foundation::core::Result<Window, WindowFull>
reserve_window(::foundation::os::ReserveCap cap, ::foundation::core::Ref<WindowDiagnostic, Brand> diagnostic) noexcept {
    return detail::reserve_window_(cap, *diagnostic);
}

// Writes the record of the host window into the host slot of the table.  The
// record is not empty.  Only the init of the root calls it, one time, before
// it gives a block of the window.
void publish_host_window(WindowRecord record, WindowTable& table) noexcept;

// Writes the record of the window of a device into the first empty slot after
// the host slot, and gives that slot.  When each of the kWindowSlots - 1
// slots of a device holds a record, the door writes nothing and gives
// table_full.  The record is not empty.  Only the init of the window of a
// device calls it, one time for each device, and the caller holds the table
// alone.  Complexity: O(kWindowSlots).
[[nodiscard]] ::foundation::core::Result<WindowSlot, WindowTableError>
publish_device_window(WindowRecord record, WindowTable& table) noexcept;

}  // namespace foundation::alloc
