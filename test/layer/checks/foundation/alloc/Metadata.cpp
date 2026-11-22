// The compile-time checks of foundation/alloc/Metadata.h.

#include <foundation/alloc/Metadata.h>

#include <foundation/alloc/ChunkMap.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <type_traits>

namespace foundation::alloc {

// A ChunkMeta is one cache line, and each byte is a named member: owner,
// placement, partition, page kind, state, its padding, the unit map, its
// padding, the decay stamp, the source word, the mark stamp and 24 spare
// bytes.
static_assert(sizeof(ChunkMeta) == kChunkMetaBytes && alignof(ChunkMeta) == 64);
static_assert(ChunkMeta::has_named_bytes());

// A SlabMeta is a header of one cache line and a bitmap of 512 bytes, and
// each byte of the header is a named member: the class, its padding, the free
// count, its padding, the summary, the three links of the slab heap, 36 spare
// bytes and the bitmap.
static_assert(sizeof(SlabMeta) == kSlabMetaBytes && alignof(SlabMeta) == 64);
static_assert(kSlabBitmapWords == 64);
static_assert(SlabMeta::has_named_bytes());

// A new record reads as the state of a chunk or a unit that no heap took, and
// a new slab is in no slab heap.
static_assert(ChunkMeta{}.owner() == ChunkOwner::none && ChunkMeta{}.state() == ChunkState::clean);
static_assert(SlabMeta{}.free_count().raw() == 0 && SlabMeta{}.summary() == SummaryBits{});
static_assert(SlabMeta{}.heap_child() == SlabLink::none && SlabMeta{}.heap_next() == SlabLink::none
              && SlabMeta{}.heap_prev() == SlabLink::none);

// The link of the slab 0 is not the link none, and the number of a slab keeps
// the order of the addresses: the last unit of a chunk comes before the first
// unit of the next chunk.
static_assert(link_of(SlabNumber{0}) != SlabLink::none);
static_assert(slab_number_of(ChunkIndex{1}, UnitIndex{core::StaticIndex<0>{}}).raw()
              == slab_number_of(ChunkIndex{0}, UnitIndex{core::StaticIndex<kUnitsPerChunk - 1>{}}).raw() + 1);

// The slot of an offset: the first byte of a slot gives its index, a byte
// inside a slot and the first byte past the slots of a slab give none, and
// the reciprocal of each class fits the bound of its proof.
static_assert(slot_at(SizeClass{9}, core::ByteCount{320}).value_or(SlabSlotIndex{core::StaticIndex<0>{}}).raw() == 2);
static_assert(slot_at(SizeClass{9}, core::ByteCount{321}).is_none());
static_assert(slot_at(SizeClass{9}, core::ByteCount{409 * 160}).is_none());
static_assert(slot_at(SizeClass{1}, core::ByteCount{kSlabUnitBytes - 16}).is_some());
static_assert(detail::kSlotFacts.rows[1].reciprocal == (std::uint64_t{1} << 43));
// The bounds of the proof: c <= 2^43, so a product of an offset below one
// chunk fits 64 bits, and c > 2^26 for a slot below one chunk, else c <= 2^26.
static_assert([] consteval {
    for (std::uint32_t value = 1; value <= kSizeClassCount; ++value) {
        detail::SlotFacts const facts = detail::kSlotFacts.rows[value];
        if (facts.reciprocal > (std::uint64_t{1} << 43)) return false;
        bool const is_below_chunk = facts.slot_bytes < kSlabChunkBytes;
        if (is_below_chunk != (facts.reciprocal > (std::uint64_t{1} << 26))) return false;
    }
    return true;
}());
static_assert(detail::kSlotFacts.rows[kSizeClassCount].slab_slots == 1);

// A record lives at its place in the region.
static_assert(!std::is_copy_constructible_v<ChunkMeta> && !std::is_copy_assignable_v<ChunkMeta>);
static_assert(!std::is_copy_constructible_v<SlabMeta> && !std::is_copy_assignable_v<SlabMeta>);

// The zero values of the enumerations are the state of a record that no heap
// took.
static_assert(Partition{} == Partition::data);
static_assert(ChunkPageKind{} == ChunkPageKind::base);
static_assert(ChunkState{} == ChunkState::clean);

namespace metadata_check {

inline constexpr std::size_t kMebibyte = std::size_t{1} << 20;

// The region of a window of 4 GiB: 2048 chunks.
inline constexpr MetadataLayout kSmall = MetadataLayout::of(::foundation::core::ElementCount{2048});

// The region of a window of 1 TiB: 524,288 chunks.
inline constexpr MetadataLayout kLarge = MetadataLayout::of(::foundation::core::ElementCount{524288});

inline constexpr ChunkIndex kLastSmallChunk{2047};
inline constexpr ChunkIndex kLastLargeChunk{524287};
inline constexpr UnitIndex kLastUnit{::foundation::core::StaticIndex<kUnitsPerChunk - 1>{}};

}  // namespace metadata_check

// Each section starts at a whole step after the guard, and the region ends
// with a guard.
static_assert(metadata_check::kSmall.map_offset().raw() == 2 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.map_bytes().raw() == 2 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.chunk_records_offset().raw() == 4 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.chunk_records_bytes().raw() == 2 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.slab_records_offset().raw() == 6 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.slab_records_bytes().raw() == 36 * metadata_check::kMebibyte);
static_assert(metadata_check::kSmall.region_bytes().raw() == 44 * metadata_check::kMebibyte);

// A window of 1 TiB: a map of 1 MiB in one step, chunk records of 32 MiB,
// and slab records of 9 GiB.
static_assert(metadata_check::kLarge.map_bytes().raw() == 2 * metadata_check::kMebibyte);
static_assert(metadata_check::kLarge.chunk_records_bytes().raw() == 32 * metadata_check::kMebibyte);
static_assert(metadata_check::kLarge.slab_records_bytes().raw() == 9216 * metadata_check::kMebibyte);
static_assert(metadata_check::kLarge.region_bytes().raw() == 9254 * metadata_check::kMebibyte);

// The records of the last chunk and of its last unit end inside their
// sections, and the last slab record ends at the last guard.
static_assert(metadata_check::kSmall.chunk_meta_offset(metadata_check::kLastSmallChunk).raw() + kChunkMetaBytes
              <= metadata_check::kSmall.slab_records_offset().raw());
static_assert(metadata_check::kSmall.slab_meta_offset(metadata_check::kLastSmallChunk, metadata_check::kLastUnit).raw()
                  + kSlabMetaBytes
              == metadata_check::kSmall.region_bytes().raw() - kMetadataGuardBytes);
static_assert(metadata_check::kLarge.slab_meta_offset(metadata_check::kLastLargeChunk, metadata_check::kLastUnit).raw()
                  + kSlabMetaBytes
              == metadata_check::kLarge.region_bytes().raw() - kMetadataGuardBytes);

}  // namespace foundation::alloc
