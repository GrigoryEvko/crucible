#pragma once

// The records of the metadata region of a window: one ChunkMeta for each
// chunk, one SlabMeta for each unit, and the layout of the region (THE
// REGION).
//
//   ChunkMeta                  the owner, the placement, the partition, the
//                              page kind, the state, the unit map, the decay
//                              stamp, the source word and the mark stamp of
//                              one chunk: one cache line
//   SlabMeta                   the class, the free count, the summary word,
//                              the three links of its slab heap and the
//                              bitmap of one unit: 576 bytes
//   SlabNumber, SlabLink       the number of a slab in its window, and a link
//                              of a slab heap: that number plus one, or none
//   MetadataLayout::of(chunks) the offset of each section of the region of
//                              a window of that many chunks, and of each
//                              record
//   slot_at(class, offset)     the slot that starts at an offset of a slab,
//                              with no division (THE SLOT OF AN OFFSET)
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
#include <foundation/Platform.h>
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

// The number of a slab in its window: the index of its chunk times
// kUnitsPerChunk, plus the index of its unit.  A medium slab is the whole
// chunk, so its number holds the unit 0.  The order of the numbers is the
// order of the addresses of the slabs.
struct SlabNumberTag final {};
using SlabNumber = ::foundation::core::Id<SlabNumberTag, std::uint32_t>;

[[nodiscard]] constexpr SlabNumber slab_number_of(ChunkIndex chunk, UnitIndex unit) noexcept {
    return SlabNumber{chunk.raw() * kUnitsPerChunk + static_cast<std::uint32_t>(unit.raw())};
}

// A link of the slab heap of foundation/alloc/SlabHeap.h: the number of a
// slab plus one, or none.  A record starts as zero bytes, and the slab 0 is a
// real slab, so zero must mean no link.
enum class SlabLink : std::uint32_t {
    none = 0,
};

[[nodiscard]] constexpr SlabLink link_of(SlabNumber slab) noexcept { return SlabLink{slab.raw() + 1}; }

// The record of one unit (THE RECORDS): a header of one cache line and a
// bitmap of 512 bytes.  A set bit is a free slot.  Bit k of the summary word
// is set when word k of the bitmap holds a free slot.  The class of a unit
// with no slab is zero, which names no class.
//
// The three links are the place of the slab in one slab heap of its owner
// (foundation/alloc/SlabHeap.h): its first child, its next sibling, and its
// last sibling or its parent.  They are named fields in the spare bytes of
// the header, so the record keeps its size and each field before them keeps
// its offset (the decision of 2026-11-21).
class alignas(64) SlabMeta final {
public:
    constexpr SlabMeta() noexcept = default;
    SlabMeta(SlabMeta const&) = delete("a record lives at its place in the region: read its fields in place");
    SlabMeta& operator=(SlabMeta const&) = delete("a record lives at its place in the region");

    [[nodiscard]] constexpr SizeClass size_class() const noexcept { return size_class_; }
    [[nodiscard]] constexpr SlabSlotCount free_count() const noexcept { return SlabSlotCount{free_count_}; }
    [[nodiscard]] constexpr SummaryBits summary() const noexcept { return summary_; }
    [[nodiscard]] constexpr SlotBits bitmap_word(BitmapWordIndex word) const noexcept { return bitmap_[word.raw()]; }
    [[nodiscard]] constexpr SlabLink heap_child() const noexcept { return heap_child_; }
    [[nodiscard]] constexpr SlabLink heap_next() const noexcept { return heap_next_; }
    [[nodiscard]] constexpr SlabLink heap_prev() const noexcept { return heap_prev_; }

    constexpr void set_size_class(SizeClass size_class) noexcept { size_class_ = size_class; }
    constexpr void set_free_count(SlabSlotCount free_count) noexcept { free_count_ = free_count.raw(); }
    constexpr void set_summary(SummaryBits summary) noexcept { summary_ = summary; }
    constexpr void set_bitmap_word(BitmapWordIndex word, SlotBits bits) noexcept { bitmap_[word.raw()] = bits; }
    constexpr void set_heap_child(SlabLink child) noexcept { heap_child_ = child; }
    constexpr void set_heap_next(SlabLink next) noexcept { heap_next_ = next; }
    constexpr void set_heap_prev(SlabLink prev) noexcept { heap_prev_ = prev; }

    // True when each field is at its offset, so each byte of the header is a
    // named field and the bitmap starts at the second line (THE RECORDS).
    // The check file of this header holds it.
    [[nodiscard]] static consteval bool has_named_bytes() noexcept {
        return offsetof(SlabMeta, size_class_) == 0 && offsetof(SlabMeta, class_padding_) == 1
            && offsetof(SlabMeta, free_count_) == 2 && offsetof(SlabMeta, count_padding_) == 4
            && offsetof(SlabMeta, summary_) == 8 && offsetof(SlabMeta, heap_child_) == 16
            && offsetof(SlabMeta, heap_next_) == 20 && offsetof(SlabMeta, heap_prev_) == 24
            && offsetof(SlabMeta, spare_) == 28 && sizeof(spare_) == 36 && offsetof(SlabMeta, bitmap_) == 64;
    }

private:
    SizeClass size_class_{};
    std::uint8_t class_padding_{0};
    std::uint16_t free_count_{0};
    std::uint32_t count_padding_{0};
    SummaryBits summary_{};
    SlabLink heap_child_{SlabLink::none};
    SlabLink heap_next_{SlabLink::none};
    SlabLink heap_prev_{SlabLink::none};
    std::uint8_t spare_[36]{};
    SlotBits bitmap_[kSlabBitmapWords]{};
};

inline constexpr std::size_t kChunkMetaBytes = 64;
inline constexpr std::size_t kSlabMetaBytes = 576;

// ── The slot of an offset ──────────────────────────────────────────────
//
// THE SLOT OF AN OFFSET
//   The flush of a bin finds the slot of each address that it gives back: the
//   offset of the address from the first byte of its slab, over the bytes of
//   the slot of the class.  A slot is not a power of two above 128 bytes, so
//   each class holds a reciprocal in place of a division: the index of the
//   slot is (offset * m) >> kSlotReciprocalShift, with m = ceil(2^32 / slot),
//   as the div_info of jemalloc divides by a slot.  Then the test
//   index * slot == offset refuses an offset inside a slot, and a second test
//   refuses an index past the slots of the slab.  So the index must be exact
//   only for an offset that is a whole count of the slot.  With m * slot =
//   2^32 + e and e < slot, the error of such an offset is index * e / 2^32,
//   and index * e < offset < 2^21, so the error stays below 1.  An offset
//   lies below one chunk, so the product stays below 2^49.

// The shift of the reciprocal of a slot (THE SLOT OF AN OFFSET).
inline constexpr std::uint32_t kSlotReciprocalShift = 32;

// The index of a slot in a slab: below the 4096 slots of the smallest class.
using SlabSlotIndex = ::foundation::core::Index<kSlabBitmapWords * 64>;

namespace detail {

// The facts of the slot of one class: its reciprocal, its bytes and the
// slots of its slab.
struct SlotFacts final {
    std::uint64_t reciprocal = 0;
    std::uint32_t slot_bytes = 0;
    std::uint32_t slab_slots = 0;
};

// The facts of the classes 1 to 64, at the index of the class.  Index 0 names
// no class, and its facts hold zero.
struct SlotFactTable final {
    SlotFacts rows[kSizeClassCount + 1]{};
};

[[nodiscard]] consteval SlotFactTable build_slot_facts_() noexcept {
    SlotFactTable table{};
    for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
        SizeClass const size_class{static_cast<std::uint8_t>(value)};
        std::uint64_t const slot = slot_bytes(size_class).raw();
        table.rows[value].reciprocal = ((std::uint64_t{1} << kSlotReciprocalShift) + slot - 1) / slot;
        table.rows[value].slot_bytes = static_cast<std::uint32_t>(slot);
        table.rows[value].slab_slots = slab_slots(size_class);
    }
    return table;
}

inline constexpr SlotFactTable kSlotFacts = build_slot_facts_();

// The index of the slot of the class of FACTS whose first byte is OFFSET
// bytes after the first byte of its slab, for an offset that is a whole count
// of the slot (THE SLOT OF AN OFFSET).  For each other offset below one chunk
// it is the floor of the quotient, or that floor plus one when the fraction
// is above 1 - 2^-11, and the test index * slot == offset refuses it.  The
// flush calls it with the facts of its class in registers.  Complexity: O(1),
// one multiply and one shift.
[[nodiscard]] constexpr std::uint64_t slot_quotient(SlotFacts const& facts,
                                                    ::foundation::core::ByteCount offset) noexcept {
    return (offset.raw() * facts.reciprocal) >> kSlotReciprocalShift;
}

}  // namespace detail

// The slot whose first byte is OFFSET bytes after the first byte of a slab of
// the class, or none for an offset that is not the first byte of a slot of
// the slab (THE SLOT OF AN OFFSET).  The flush of a bin in the static library
// foundation calls it, so the check of the class is a fatal invariant, as in
// MetadataLayout.  Complexity: O(1), two multiplies, one shift and three
// compares, with no division.
[[nodiscard]] constexpr ::foundation::core::Option<SlabSlotIndex>
slot_at(SizeClass size_class, ::foundation::core::ByteCount offset) noexcept {
    CRUCIBLE_FATAL_INVARIANT(is_size_class(size_class));
    std::uint64_t const bytes = offset.raw();
    if (bytes >= kSlabChunkBytes) return ::foundation::core::none;
    detail::SlotFacts const& facts = detail::kSlotFacts.rows[static_cast<std::uint8_t>(size_class)];
    std::uint64_t const index = detail::slot_quotient(facts, offset);
    if (index * facts.slot_bytes != bytes) return ::foundation::core::none;
    if (index >= facts.slab_slots) return ::foundation::core::none;
    return SlabSlotIndex::check(static_cast<std::size_t>(index));
}

// The guard before the first section and after the last section of the
// region, and the step of each commit of a section (THE REGION).
inline constexpr std::size_t kMetadataGuardBytes = std::size_t{2} << 20;
inline constexpr std::size_t kMetadataStepBytes = std::size_t{2} << 20;

// The offsets of the sections of the region of one window, and of each
// record (THE REGION).  Each offset counts from the first byte of the
// reservation of the region, the first byte of its first guard.  A wrong
// offset would write outside the region, so each check is a fatal invariant
// that holds in each build and under each contract semantic, and the window
// door of the static library foundation holds no contract machinery.
class MetadataLayout final {
public:
    constexpr MetadataLayout(MetadataLayout const&) noexcept = default;
    constexpr MetadataLayout(MetadataLayout&&) noexcept = default;
    constexpr MetadataLayout& operator=(MetadataLayout const&) noexcept = default;
    constexpr MetadataLayout& operator=(MetadataLayout&&) noexcept = default;
    ~MetadataLayout() = default;

    // The layout of the region of a window of CHUNKS chunks, which is at most
    // the chunks of a window of 1 TiB.  Complexity: O(1).
    [[nodiscard]] static constexpr MetadataLayout of(::foundation::core::ElementCount chunks) noexcept {
        CRUCIBLE_FATAL_INVARIANT(chunks.raw() != 0);
        CRUCIBLE_FATAL_INVARIANT(chunks.raw() <= kMostChunks);
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
        CRUCIBLE_FATAL_INVARIANT(chunk.raw() < chunks_);
        return ::foundation::core::ByteCount{chunk_records_offset_ + std::size_t{chunk.raw()} * kChunkMetaBytes};
    }

    // The offset of the SlabMeta of a unit of a chunk of the window.
    [[nodiscard]] constexpr ::foundation::core::ByteCount slab_meta_offset(ChunkIndex chunk,
                                                                           UnitIndex unit) const noexcept {
        CRUCIBLE_FATAL_INVARIANT(chunk.raw() < chunks_);
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
