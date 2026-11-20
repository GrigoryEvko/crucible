#pragma once

// The window of the allocator of the base: its geometry, the id of a heap,
// the owner entry of each chunk, and the record of a window (THE WINDOW).
//
//   kChunkShift, kChunkBytes       a chunk: 2 MiB at an address that is a
//                                  whole count of 2 MiB
//   kUnitsPerChunk                 the units of 64 KiB of one chunk: 32
//   kExtentShift, kExtentBytes     an extent: 1 GiB, 512 chunks
//   kChunksPerExtent               512.  The last chunk of an extent is its
//                                  guard (THE EXTENT)
//   AddressWord                    the address of a byte of a window, as a
//                                  64-bit word that no path reads through
//   strip_tag(address)             the address with no tag byte (THE TAG
//                                  BYTE)
//   HeapId                         the id of a heap, 1 to 65,534
//   ChunkOwner                     the entry of a chunk in the chunk map: no
//                                  owner, a heap, or a chunk of a span
//   ChunkIndex, UnitIndex          a chunk of a window, a unit of a chunk
//   BiasedChunkMap                 the word that reads the owner of the
//                                  chunk of an address with a shift and a
//                                  load (THE MAP READ)
//   WindowBase, RegionBase         the first byte of a window and of its
//                                  metadata region, two roles of an
//                                  AddressWord
//   WindowLength, RegionLength     the bytes of a window and of its region,
//                                  two roles of a ByteCount
//   WindowRecord                   the base, the length, the biased map and
//                                  the metadata region of one window
//   WindowTable                    the record of each window: slot 0 is the
//                                  host, and the other slots wait for the
//                                  windows of the devices
//
// THE WINDOW
//   One range of address space holds each small, medium and large block of
//   the host.  A window is a whole count of extents, and an extent is a whole
//   count of chunks.  So the chunk of a block is its address less the base of
//   the window, shifted right by kChunkShift.  A device gets a window of its
//   own, with a record of its own in the same table, so two devices never
//   share a chunk map.
//
// THE EXTENT
//   An extent of 1 GiB holds 512 chunks.  Its last chunk is never committed,
//   so a linear overflow out of the last block of one extent faults in the
//   guard before it reaches the next extent.  A window of N extents then
//   holds 511 * N chunks that a heap can own.
//
// THE OWNER ENTRY
//   The chunk map holds one ChunkOwner of 16 bits for each chunk of a window.
//   Zero means no owner, so a map that the kernel gives as zero pages is a
//   map of free chunks.  The value 0xFFFF means a chunk of a span.  Each
//   other value is the id of the heap that owns the chunk.  One heap table
//   gives the ids of the host heaps and of the device heaps, so an envelope
//   of a host thread can name a heap of a device.  HeapId is not the entry:
//   the niche of an Option<HeapId> is 0xFFFF, the value of a span.
//
// THE MAP READ
//   A BiasedChunkMap holds the address of the map less two bytes for each
//   chunk below the base of the window.  The entry of an address is then at
//   the word plus (address >> kChunkShift) * 2: one shift and one load with
//   an index, and the word is a copy in the heap state of the reader (shrq,
//   movq, movzwl on x86-64).  The word is a number, so the sum wraps with no
//   undefined behavior, and the read turns the sum into a pointer only at the
//   load.  owner_of reads only the entry of an address of its window, and
//   the caller proves that fact: a block that the allocator gave is inside
//   its window.  WindowRecord::checked_owner_of compares a foreign address
//   with the window first.
//
//   One thread writes the entry of a chunk: the thread that takes or gives
//   back the chunk, before it gives out a block of it, or after the last
//   block of it ended.  A thread that frees a block got the block through a
//   publish that happens after the write, so the read is a plain load.
//
// THE TAG BYTE
//   On aarch64 a load or a store ignores the top byte of its address (top
//   byte ignore), so a pointer can hold a tag in its bits 56 to 63, as the
//   memory tags of MTE and of HWASan do.  The chunk of a tagged address is
//   the chunk of the same address with no tag.  So owner_of, is_in_window
//   and chunk_of read each address through strip_tag, and on aarch64 the
//   owner read is one bit-field extract and one load.  A process of the base
//   on x86-64 has no tagged address: with no linear address masking, a
//   tagged address faults at its first use.  So strip_tag keeps each bit
//   there, and the owner read stays a shift and a load.  The user range of
//   Linux is 48 bits, or 52 bits with the large virtual addresses of
//   aarch64, so each window lies below 2^56.
//
// This header is in the layer F0_primitives of utils/rule-table.txt.  The
// window door foundation/alloc/Window.h builds each record and each map.

#include <foundation/ByteSeal.h>
#include <foundation/alloc/Layout.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Side.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

inline constexpr std::uint32_t kChunkShift = 21;
inline constexpr std::size_t kChunkBytes = std::size_t{1} << kChunkShift;
inline constexpr std::uint32_t kUnitShift = 16;
inline constexpr std::uint32_t kUnitsPerChunk = std::uint32_t{1} << (kChunkShift - kUnitShift);
inline constexpr std::uint32_t kExtentShift = 30;
inline constexpr std::size_t kExtentBytes = std::size_t{1} << kExtentShift;
inline constexpr std::uint32_t kChunksPerExtent = std::uint32_t{1} << (kExtentShift - kChunkShift);

// The address of a byte of a window, as a word.  A device window gives
// numbers that the host cannot read, so no path of the engine reads through
// a word or does pointer arithmetic on it.
enum class AddressWord : std::uint64_t {
};

// The top bits of an address that hold a tag and name no byte (THE TAG
// BYTE): 8 on aarch64, and none on x86-64.  The other bits name the byte.
#if defined(__aarch64__)
inline constexpr std::uint32_t kTagBits = 8;
#else
inline constexpr std::uint32_t kTagBits = 0;
#endif
inline constexpr std::uint64_t kAddressMask = ~std::uint64_t{0} >> kTagBits;

// The address with no tag byte (THE TAG BYTE).  Complexity: O(1), one and
// on aarch64, and no instruction on x86-64.
[[nodiscard]] constexpr AddressWord strip_tag(AddressWord address) noexcept {
    return AddressWord{static_cast<std::uint64_t>(address) & kAddressMask};
}

namespace side {

// The roles of the words of a window record: the first byte and the bytes of
// the window, and the first byte and the bytes of its metadata region.
struct WindowBase final {};
struct RegionBase final {};
struct WindowLength final {};
struct RegionLength final {};

}  // namespace side

// The first byte of a window and of its region, and the bytes of each.  Each
// role is its own type, so the door that builds a record cannot swap two of
// them.
using WindowBase = ::foundation::core::Side<side::WindowBase, AddressWord>;
using RegionBase = ::foundation::core::Side<side::RegionBase, AddressWord>;
using WindowLength = ::foundation::core::Side<side::WindowLength, ::foundation::core::ByteCount>;
using RegionLength = ::foundation::core::Side<side::RegionLength, ::foundation::core::ByteCount>;

// The id of a heap: 1 to kLastHeapId (THE OWNER ENTRY).
struct HeapIdTag final {};
using HeapId = ::foundation::core::Id<HeapIdTag, std::uint16_t>;

// The count of the heaps of the heap table, host and device together.
inline constexpr std::uint32_t kHeapCount = 65534;
inline constexpr std::uint16_t kFirstHeapId = 1;
inline constexpr std::uint16_t kLastHeapId = 65534;

// The slot of a heap in the heap table.  Slot S holds the heap of the id
// S + 1.
using HeapSlot = ::foundation::core::Index<kHeapCount>;

[[nodiscard]] constexpr HeapId heap_id_of(HeapSlot slot) noexcept {
    return HeapId{static_cast<std::uint16_t>(slot.raw() + kFirstHeapId)};
}

// The entry of a chunk in the chunk map (THE OWNER ENTRY).  Each value
// between none and span is the id of a heap.
enum class ChunkOwner : std::uint16_t {
    none = 0,
    span = 0xFFFF,
};

[[nodiscard]] constexpr ChunkOwner owner_entry_of(HeapId heap) noexcept { return ChunkOwner{heap.raw()}; }

// The heap of an entry, or none for a chunk with no owner and for a chunk of
// a span.
[[nodiscard]] constexpr ::foundation::core::Option<HeapId> heap_of(ChunkOwner owner) noexcept {
    if (owner == ChunkOwner::none || owner == ChunkOwner::span) return ::foundation::core::none;
    return ::foundation::core::Option<HeapId>::some(HeapId{static_cast<std::uint16_t>(owner)});
}

// The index of a chunk in its window, from zero at the base.
struct ChunkIndexTag final {};
using ChunkIndex = ::foundation::core::Id<ChunkIndexTag, std::uint32_t>;

// The index of a unit of 64 KiB in its chunk.
using UnitIndex = ::foundation::core::Index<kUnitsPerChunk>;

// True when the chunk is the guard of its extent (THE EXTENT).
[[nodiscard]] constexpr bool is_guard_chunk(ChunkIndex chunk) noexcept {
    return (chunk.raw() & (kChunksPerExtent - 1)) == kChunksPerExtent - 1;
}

class WindowRecord;

namespace detail {

// The one builder of a BiasedChunkMap and of a WindowRecord, and the one
// writer of a WindowTable.  src/foundation/alloc/Window.cpp defines it, so
// only the window door makes a map or a record.
struct WindowRecordDoor;

}  // namespace detail

// The word of the chunk map of one window (THE MAP READ).  The empty map
// holds the word zero, and it names no window: the sentinel heap holds it
// until the init of the root.  A forged word would read each address, so
// the map and the record below hold a byte_seal of foundation/ByteSeal.h:
// their assignments are user-provided, so std::bit_cast builds neither from
// bytes, and their copies stay trivial, so a call passes them in registers.
class BiasedChunkMap final {
public:
    constexpr BiasedChunkMap() noexcept = default;
    constexpr BiasedChunkMap(BiasedChunkMap const&) noexcept = default;
    constexpr BiasedChunkMap(BiasedChunkMap&&) noexcept = default;
    constexpr BiasedChunkMap& operator=(BiasedChunkMap const&) noexcept = default;
    constexpr BiasedChunkMap& operator=(BiasedChunkMap&&) noexcept = default;
    ~BiasedChunkMap() = default;

    // The entry of the chunk of an address of the window of the map, with or
    // without a tag byte.  The caller proves that the address is inside the
    // window.  Complexity: O(1), one shift and one load on x86-64, one
    // bit-field extract and one load on aarch64 (THE TAG BYTE).
    [[nodiscard]] ChunkOwner owner_of(AddressWord address) const noexcept {
        // The shift left drops the tag, as strip_tag does.  GCC makes this
        // form one ubfx and one load with a scaled index on aarch64.  A mask
        // gives a lsr and a ubfiz there, one more instruction.
        std::uint64_t const chunk = (static_cast<std::uint64_t>(address) << kTagBits) >> (kTagBits + kChunkShift);
        return *__builtin_bit_cast(ChunkOwner const*, word_ + (chunk << 1));
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return word_ == 0; }

    [[nodiscard]] friend constexpr bool operator==(BiasedChunkMap left, BiasedChunkMap right) noexcept {
        return left.word_ == right.word_;
    }

private:
    friend struct detail::WindowRecordDoor;

    constexpr explicit BiasedChunkMap(std::uint64_t word) noexcept : word_{word} {}

    std::uint64_t word_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The record of one window: its base, its length, its biased map and its
// metadata region (THE WINDOW).  The empty record names no window and holds
// no byte.  Each field is a word of 8 bytes, so a copy of a record moves
// whole words.
class WindowRecord final {
public:
    constexpr WindowRecord() noexcept = default;
    constexpr WindowRecord(WindowRecord const&) noexcept = default;
    constexpr WindowRecord(WindowRecord&&) noexcept = default;
    constexpr WindowRecord& operator=(WindowRecord const&) noexcept = default;
    constexpr WindowRecord& operator=(WindowRecord&&) noexcept = default;
    ~WindowRecord() = default;

    [[nodiscard]] constexpr AddressWord base() const noexcept { return AddressWord{base_}; }
    [[nodiscard]] constexpr ::foundation::core::ByteCount length() const noexcept {
        return ::foundation::core::ByteCount{length_};
    }
    [[nodiscard]] constexpr BiasedChunkMap chunk_map() const noexcept { return chunk_map_; }
    [[nodiscard]] constexpr AddressWord region_base() const noexcept { return AddressWord{region_base_}; }
    [[nodiscard]] constexpr ::foundation::core::ByteCount region_length() const noexcept {
        return ::foundation::core::ByteCount{region_length_};
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return length_ == 0; }

    // True when the address, with or without a tag byte, is inside the
    // window (THE TAG BYTE).  One subtraction and one compare, and the empty
    // record holds no address.
    [[nodiscard]] constexpr bool is_in_window(AddressWord address) const noexcept {
        return static_cast<std::uint64_t>(strip_tag(address)) - base_ < length_;
    }

    // The count of the chunks of the window.
    [[nodiscard]] constexpr std::uint64_t chunk_count() const noexcept { return length_ >> kChunkShift; }

    // The chunk of an address of the window, with or without a tag byte.
    [[nodiscard]] constexpr ChunkIndex chunk_of(AddressWord address) const noexcept {
        CRUCIBLE_PRE(is_in_window(address));
        std::uint64_t const offset = static_cast<std::uint64_t>(strip_tag(address)) - base_;
        return ChunkIndex{static_cast<std::uint32_t>(offset >> kChunkShift)};
    }

    // The address of the first byte of a chunk of the window.
    [[nodiscard]] constexpr AddressWord address_of(ChunkIndex chunk) const noexcept {
        CRUCIBLE_PRE(chunk.raw() < chunk_count());
        return AddressWord{base_ + (std::uint64_t{chunk.raw()} << kChunkShift)};
    }

    // The entry of the chunk of a foreign address, or none for an address
    // outside the window.  Complexity: O(1).
    [[nodiscard]] ::foundation::core::Option<ChunkOwner> checked_owner_of(AddressWord address) const noexcept {
        if (!is_in_window(address)) return ::foundation::core::none;
        return ::foundation::core::Option<ChunkOwner>::some(chunk_map_.owner_of(address));
    }

private:
    friend struct detail::WindowRecordDoor;

    constexpr WindowRecord(WindowBase base, WindowLength length, BiasedChunkMap chunk_map, RegionBase region_base,
                           RegionLength region_length) noexcept
        : base_{static_cast<std::uint64_t>(base.value())},
          length_{length.value().raw()},
          chunk_map_{chunk_map},
          region_base_{static_cast<std::uint64_t>(region_base.value())},
          region_length_{region_length.value().raw()} {}

    std::uint64_t base_ = 0;
    std::uint64_t length_ = 0;
    BiasedChunkMap chunk_map_{};
    std::uint64_t region_base_ = 0;
    std::uint64_t region_length_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The count of the slots of the table of windows: the host and 15 devices.
inline constexpr std::size_t kWindowSlots = 16;

// The slot of a window in the table.
using WindowSlot = ::foundation::core::Index<kWindowSlots>;

// The slot of the host window.
inline constexpr WindowSlot kHostWindow{::foundation::core::StaticIndex<0>{}};

// The record of each window (THE WINDOW).  A new table holds the empty record
// in each slot.  The window door writes the record of a slot, one time, before
// it gives a block of the window, and only the init of the root calls it.
// After that, each read is a plain load.
class WindowTable final {
public:
    constexpr WindowTable() noexcept = default;

    WindowTable(WindowTable const&) = delete("the table holds the one record of each window");
    WindowTable& operator=(WindowTable const&) = delete("the table holds the one record of each window");

    // A record is five words, so each read gives a copy.
    [[nodiscard]] constexpr WindowRecord record(WindowSlot slot) const noexcept { return records_[slot.raw()]; }

    [[nodiscard]] constexpr WindowRecord host() const noexcept { return records_[kHostWindow.raw()]; }

private:
    friend struct detail::WindowRecordDoor;

    // A plain array: core/Array.h includes core/Heap.h, and the allocation
    // door of core/Heap.h reads this header.  A WindowSlot is below the
    // count, so each subscript is inside the array.
    WindowRecord records_[kWindowSlots]{};
};

}  // namespace foundation::alloc
