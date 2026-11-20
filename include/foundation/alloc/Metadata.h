#pragma once

// The records of the metadata region of a window: one ChunkMeta for each
// chunk, one SlabMeta for each unit, and the layout of the region (THE
// REGION).
//
//   ChunkMeta                  the owner, the placement, the partition, the
//                              page kind, the state, the unit map, the decay
//                              stamp, the source word and the mark stamp of
//                              one chunk: one cache line
//   SlabMeta                   the class, the free count, the summary word
//                              and the bitmap of one unit: 576 bytes
//   MetadataLayout::of(chunks) the offset of each section of the region of
//                              a window of that many chunks, and of each
//                              record
//
// THE REGION
//   No block holds a header, a link or a canary.  Each fact of the allocator
//   about a block is in one region of its own, beside the window, so a write
//   past a block cannot change the state of the allocator, and a device
//   window keeps its records in host memory.  The region has a guard of
//   kMetadataGuardBytes before its first section and after its last, and
//   three sections, each at a whole count of kMetadataStepBytes:
//     the chunk map        one ChunkOwner of 2 bytes for each chunk
//     the chunk records    one ChunkMeta of 64 bytes for each chunk
//     the slab records     one SlabMeta of 576 bytes for each unit
//   The offset of each record is a constant function of the index of its
//   chunk and of its unit.  A window of 1 TiB has 524,288 chunks: a map of
//   1 MiB, chunk records of 32 MiB and slab records of 9 GiB of address
//   space.  The kernel gives a page frame only at the first write.
//
// THE RECORDS
//   A record starts as zero bytes, which is the state of a chunk or a unit
//   that no heap took: no owner, the placement 0, the partition data, base
//   pages, never written, and no class.  Each padding byte is a named member
//   that stays zero.  A record lives at its place in the region, so it does
//   not copy or move, and each reader reads a field in place.  So no copy
//   of a record moves fields of mixed widths through memory.
//
//   One thread writes a record: the owner heap of the chunk, or the thread
//   that takes or gives back the chunk.  A heap of a device reads and writes
//   these host records too.  The page source of the chunk owns source_word,
//   and the host source leaves it zero.  mark_stamp is the mark of a queue of
//   a device, and the host leaves it zero.
//
// This header is in the layer F0_primitives of utils/rule-table.txt.  It
// holds plain arrays and includes no core/Array.h, because core/Array.h
// includes core/Heap.h, and the allocation door of core/Heap.h reads the
// headers of foundation/alloc.

#include <foundation/ByteSeal.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Layout.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <cstdint>

namespace foundation::alloc {

// The partition of a chunk.  Each partition takes its extents apart from the
// others.  The zero value is data, the state of a record that no heap took.
enum class Partition : std::uint8_t {
    data = 0,
    pointer = 1,
    untrusted = 2,
    secret = 3,
};

// The pages of a chunk: base pages, or one huge page.
enum class ChunkPageKind : std::uint8_t {
    base = 0,
    huge = 1,
};

// The state of a chunk.  A chunk that no heap took is clean, dirty or
// purged: never written, committed with pages on the list of its node, or
// given back to the kernel with its range still reserved.
enum class ChunkState : std::uint8_t {
    clean = 0,
    owned = 1,
    dirty = 2,
    purged = 3,
};

// The placement of a chunk: a NUMA node of the host, or a placement that the
// page source of a device names.
struct PlacementIdTag final {};
using PlacementId = ::foundation::core::Id<PlacementIdTag, std::uint16_t>;

// The free units of a chunk of large runs, one bit for each unit.  A set bit
// is a free unit.
enum class UnitMap : std::uint32_t {
};

// The tick of the heap at the last time that the chunk became free.
enum class DecayStamp : std::uint64_t {
};

// The word of the page source of the chunk, such as the grant index of a
// device.  The host source leaves it zero.
enum class SourceWord : std::uint64_t {
};

// The mark of the last queue of a device that used the chunk.  The host
// leaves it zero.
enum class MarkStamp : std::uint64_t {
};

// The record of one chunk (THE RECORDS): one cache line.  Each read gives a
// field by value, and each write sets one field, so no caller holds the
// address of a field.
class alignas(64) ChunkMeta final {
public:
    constexpr ChunkMeta() noexcept = default;
    ChunkMeta(ChunkMeta const&) = delete("a record lives at its place in the region: read its fields in place");
    ChunkMeta& operator=(ChunkMeta const&) = delete("a record lives at its place in the region");

    [[nodiscard]] constexpr ChunkOwner owner() const noexcept { return owner_; }
    [[nodiscard]] constexpr PlacementId placement() const noexcept { return placement_; }
    [[nodiscard]] constexpr Partition partition() const noexcept { return partition_; }
    [[nodiscard]] constexpr ChunkPageKind page_kind() const noexcept { return page_kind_; }
    [[nodiscard]] constexpr ChunkState state() const noexcept { return state_; }
    [[nodiscard]] constexpr UnitMap free_units() const noexcept { return free_units_; }
    [[nodiscard]] constexpr DecayStamp decay_stamp() const noexcept { return decay_stamp_; }
    [[nodiscard]] constexpr SourceWord source_word() const noexcept { return source_word_; }
    [[nodiscard]] constexpr MarkStamp mark_stamp() const noexcept { return mark_stamp_; }

    constexpr void set_owner(ChunkOwner owner) noexcept { owner_ = owner; }
    constexpr void set_placement(PlacementId placement) noexcept { placement_ = placement; }
    constexpr void set_partition(Partition partition) noexcept { partition_ = partition; }
    constexpr void set_page_kind(ChunkPageKind page_kind) noexcept { page_kind_ = page_kind; }
    constexpr void set_state(ChunkState state) noexcept { state_ = state; }
    constexpr void set_free_units(UnitMap free_units) noexcept { free_units_ = free_units; }
    constexpr void set_decay_stamp(DecayStamp decay_stamp) noexcept { decay_stamp_ = decay_stamp; }
    constexpr void set_source_word(SourceWord source_word) noexcept { source_word_ = source_word; }
    constexpr void set_mark_stamp(MarkStamp mark_stamp) noexcept { mark_stamp_ = mark_stamp; }

    // True when each field is at its offset, so each byte of the line is a
    // named field (THE RECORDS).  The check file of this header holds it.
    [[nodiscard]] static consteval bool has_named_bytes() noexcept {
        return offsetof(ChunkMeta, owner_) == 0 && offsetof(ChunkMeta, placement_) == 2
            && offsetof(ChunkMeta, partition_) == 4 && offsetof(ChunkMeta, page_kind_) == 5
            && offsetof(ChunkMeta, state_) == 6 && offsetof(ChunkMeta, state_padding_) == 7
            && offsetof(ChunkMeta, free_units_) == 8 && offsetof(ChunkMeta, unit_padding_) == 12
            && offsetof(ChunkMeta, decay_stamp_) == 16 && offsetof(ChunkMeta, source_word_) == 24
            && offsetof(ChunkMeta, mark_stamp_) == 32 && offsetof(ChunkMeta, spare_) == 40 && sizeof(spare_) == 24;
    }

private:
    ChunkOwner owner_{ChunkOwner::none};
    PlacementId placement_{0};
    Partition partition_{Partition::data};
    ChunkPageKind page_kind_{ChunkPageKind::base};
    ChunkState state_{ChunkState::clean};
    std::uint8_t state_padding_{0};
    UnitMap free_units_{};
    std::uint32_t unit_padding_{0};
    DecayStamp decay_stamp_{};
    SourceWord source_word_{};
    MarkStamp mark_stamp_{};
    std::uint8_t spare_[24]{};
};

// The count of the words of the bitmap of a slab: 4096 slots of the smallest
// class in one unit.
inline constexpr std::size_t kSlabBitmapWords = kSlabUnitBytes / kSlotStepBytes / 64;

// The index of a word of the bitmap of a slab.
using BitmapWordIndex = ::foundation::core::Index<kSlabBitmapWords>;

// The free slots of a slab: at most 4096.
using SlabSlotCount = ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint16_t>;

// One word of the bitmap of a slab: bit k is set when slot k of the word is
// free.
enum class SlotBits : std::uint64_t {
};

// The summary of a slab: bit k is set when word k of the bitmap holds a free
// slot.
enum class SummaryBits : std::uint64_t {
};

// The record of one unit (THE RECORDS): a header of one cache line and a
// bitmap of 512 bytes.  A set bit is a free slot.  Bit k of the summary word
// is set when word k of the bitmap holds a free slot.  The class of a unit
// with no slab is zero, which names no class.
class alignas(64) SlabMeta final {
public:
    constexpr SlabMeta() noexcept = default;
    SlabMeta(SlabMeta const&) = delete("a record lives at its place in the region: read its fields in place");
    SlabMeta& operator=(SlabMeta const&) = delete("a record lives at its place in the region");

    [[nodiscard]] constexpr SizeClass size_class() const noexcept { return size_class_; }
    [[nodiscard]] constexpr SlabSlotCount free_count() const noexcept { return SlabSlotCount{free_count_}; }
    [[nodiscard]] constexpr SummaryBits summary() const noexcept { return summary_; }
    [[nodiscard]] constexpr SlotBits bitmap_word(BitmapWordIndex word) const noexcept { return bitmap_[word.raw()]; }

    constexpr void set_size_class(SizeClass size_class) noexcept { size_class_ = size_class; }
    constexpr void set_free_count(SlabSlotCount free_count) noexcept { free_count_ = free_count.raw(); }
    constexpr void set_summary(SummaryBits summary) noexcept { summary_ = summary; }
    constexpr void set_bitmap_word(BitmapWordIndex word, SlotBits bits) noexcept { bitmap_[word.raw()] = bits; }

    // True when each field is at its offset, so each byte of the header is a
    // named field and the bitmap starts at the second line (THE RECORDS).
    // The check file of this header holds it.
    [[nodiscard]] static consteval bool has_named_bytes() noexcept {
        return offsetof(SlabMeta, size_class_) == 0 && offsetof(SlabMeta, class_padding_) == 1
            && offsetof(SlabMeta, free_count_) == 2 && offsetof(SlabMeta, count_padding_) == 4
            && offsetof(SlabMeta, summary_) == 8 && offsetof(SlabMeta, spare_) == 16 && sizeof(spare_) == 48
            && offsetof(SlabMeta, bitmap_) == 64;
    }

private:
    SizeClass size_class_{};
    std::uint8_t class_padding_{0};
    std::uint16_t free_count_{0};
    std::uint32_t count_padding_{0};
    SummaryBits summary_{};
    std::uint8_t spare_[48]{};
    SlotBits bitmap_[kSlabBitmapWords]{};
};

inline constexpr std::size_t kChunkMetaBytes = 64;
inline constexpr std::size_t kSlabMetaBytes = 576;

// The guard before the first section and after the last section of the
// region, and the step of each commit of a section (THE REGION).
inline constexpr std::size_t kMetadataGuardBytes = std::size_t{2} << 20;
inline constexpr std::size_t kMetadataStepBytes = std::size_t{2} << 20;

// The offsets of the sections of the region of one window, and of each
// record (THE REGION).  Each offset counts from the first byte of the
// reservation of the region, the first byte of its first guard.
class MetadataLayout final {
public:
    // The layout of the region of a window of CHUNKS chunks, which is at most
    // the chunks of a window of 1 TiB.  Complexity: O(1).
    [[nodiscard]] static constexpr MetadataLayout of(::foundation::core::ElementCount chunks) noexcept {
        CRUCIBLE_PRE(chunks.raw() != 0 && chunks.raw() <= kMostChunks);
        std::size_t const count = chunks.raw();
        MetadataLayout layout{};
        layout.chunks_ = count;
        layout.map_offset_ = kMetadataGuardBytes;
        layout.chunk_records_offset_ = layout.map_offset_ + whole_steps_(count * sizeof(ChunkOwner));
        layout.slab_records_offset_ = layout.chunk_records_offset_ + whole_steps_(count * kChunkMetaBytes);
        layout.end_offset_ = layout.slab_records_offset_ + whole_steps_(count * kUnitsPerChunk * kSlabMetaBytes);
        return layout;
    }

    [[nodiscard]] constexpr ::foundation::core::ElementCount chunk_count() const noexcept {
        return ::foundation::core::ElementCount{chunks_};
    }

    // The bytes of the reservation of the region, with its two guards.
    [[nodiscard]] constexpr ::foundation::core::ByteCount region_bytes() const noexcept {
        return ::foundation::core::ByteCount{end_offset_ + kMetadataGuardBytes};
    }

    [[nodiscard]] constexpr ::foundation::core::ByteCount map_offset() const noexcept {
        return ::foundation::core::ByteCount{map_offset_};
    }
    [[nodiscard]] constexpr ::foundation::core::ByteCount map_bytes() const noexcept {
        return ::foundation::core::ByteCount{chunk_records_offset_ - map_offset_};
    }
    [[nodiscard]] constexpr ::foundation::core::ByteCount chunk_records_offset() const noexcept {
        return ::foundation::core::ByteCount{chunk_records_offset_};
    }
    [[nodiscard]] constexpr ::foundation::core::ByteCount chunk_records_bytes() const noexcept {
        return ::foundation::core::ByteCount{slab_records_offset_ - chunk_records_offset_};
    }
    [[nodiscard]] constexpr ::foundation::core::ByteCount slab_records_offset() const noexcept {
        return ::foundation::core::ByteCount{slab_records_offset_};
    }
    [[nodiscard]] constexpr ::foundation::core::ByteCount slab_records_bytes() const noexcept {
        return ::foundation::core::ByteCount{end_offset_ - slab_records_offset_};
    }

    // The offset of the ChunkMeta of a chunk of the window.
    [[nodiscard]] constexpr ::foundation::core::ByteCount chunk_meta_offset(ChunkIndex chunk) const noexcept {
        CRUCIBLE_PRE(chunk.raw() < chunks_);
        return ::foundation::core::ByteCount{chunk_records_offset_ + std::size_t{chunk.raw()} * kChunkMetaBytes};
    }

    // The offset of the SlabMeta of a unit of a chunk of the window.
    [[nodiscard]] constexpr ::foundation::core::ByteCount slab_meta_offset(ChunkIndex chunk,
                                                                           UnitIndex unit) const noexcept {
        CRUCIBLE_PRE(chunk.raw() < chunks_);
        std::size_t const slab = std::size_t{chunk.raw()} * kUnitsPerChunk + unit.raw();
        return ::foundation::core::ByteCount{slab_records_offset_ + slab * kSlabMetaBytes};
    }

private:
    // The chunks of a window of 1 TiB, the first size of the ladder.
    static constexpr std::size_t kMostChunks = (std::size_t{1} << 40) >> kChunkShift;

    // The bytes rounded up to a whole count of kMetadataStepBytes.
    [[nodiscard]] static constexpr std::size_t whole_steps_(std::size_t bytes) noexcept {
        return (bytes + (kMetadataStepBytes - 1)) & ~(kMetadataStepBytes - 1);
    }

    constexpr MetadataLayout() noexcept = default;

    std::size_t chunks_ = 0;
    std::size_t map_offset_ = 0;
    std::size_t chunk_records_offset_ = 0;
    std::size_t slab_records_offset_ = 0;
    std::size_t end_offset_ = 0;
    // The assignments are user-provided, so std::bit_cast builds no layout
    // from bytes (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

}  // namespace foundation::alloc
