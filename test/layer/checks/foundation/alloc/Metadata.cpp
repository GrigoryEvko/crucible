// The compile-time checks of foundation/alloc/Metadata.h.

#include <foundation/alloc/Metadata.h>

#include <foundation/alloc/ChunkMap.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace foundation::alloc {

// A ChunkMeta is one cache line, and each byte is a named member.
static_assert(sizeof(ChunkMeta) == kChunkMetaBytes && alignof(ChunkMeta) == 64);
static_assert(std::meta::offset_of(^^ChunkMeta::owner).bytes == 0);
static_assert(std::meta::offset_of(^^ChunkMeta::placement).bytes == 2);
static_assert(std::meta::offset_of(^^ChunkMeta::partition).bytes == 4);
static_assert(std::meta::offset_of(^^ChunkMeta::page_kind).bytes == 5);
static_assert(std::meta::offset_of(^^ChunkMeta::state).bytes == 6);
static_assert(std::meta::offset_of(^^ChunkMeta::state_padding).bytes == 7);
static_assert(std::meta::offset_of(^^ChunkMeta::free_units).bytes == 8);
static_assert(std::meta::offset_of(^^ChunkMeta::unit_padding).bytes == 12);
static_assert(std::meta::offset_of(^^ChunkMeta::decay_stamp).bytes == 16);
static_assert(std::meta::offset_of(^^ChunkMeta::source_word).bytes == 24);
static_assert(std::meta::offset_of(^^ChunkMeta::mark_stamp).bytes == 32);
static_assert(std::meta::offset_of(^^ChunkMeta::spare).bytes == 40);
static_assert(sizeof(ChunkMeta::spare) == 24);

// A SlabMeta is a header of one cache line and a bitmap of 512 bytes, and
// each byte of the header is a named member.
static_assert(sizeof(SlabMeta) == kSlabMetaBytes && alignof(SlabMeta) == 64);
static_assert(kSlabBitmapWords == 64);
static_assert(std::meta::offset_of(^^SlabMeta::size_class).bytes == 0);
static_assert(std::meta::offset_of(^^SlabMeta::class_padding).bytes == 1);
static_assert(std::meta::offset_of(^^SlabMeta::free_count).bytes == 2);
static_assert(std::meta::offset_of(^^SlabMeta::count_padding).bytes == 4);
static_assert(std::meta::offset_of(^^SlabMeta::summary).bytes == 8);
static_assert(std::meta::offset_of(^^SlabMeta::spare).bytes == 16);
static_assert(std::meta::offset_of(^^SlabMeta::bitmap).bytes == 64);

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
