// The compile-time checks of foundation/alloc/ChunkMap.h.

#include <foundation/alloc/ChunkMap.h>

#include <foundation/alloc/Layout.h>
#include <foundation/core/Index.h>

#include <cstdint>
#include <type_traits>

namespace foundation::alloc {

// The geometry: a chunk of 2 MiB is the chunk of a medium slab, a unit is the
// unit of a small slab, and an extent of 1 GiB holds 512 chunks.
static_assert(kChunkBytes == kSlabChunkBytes);
static_assert(kUnitsPerChunk == 32 && kSlabUnitBytes * kUnitsPerChunk == kChunkBytes);
static_assert(kChunksPerExtent == 512 && kExtentBytes == kChunkBytes * kChunksPerExtent);

// The owner entry is 16 bits, zero bytes are no owner, and 0xFFFF is a span.
static_assert(sizeof(ChunkOwner) == 2);
static_assert(ChunkOwner{} == ChunkOwner::none);
static_assert(static_cast<std::uint16_t>(ChunkOwner::span) == 0xFFFF);

// The ids of the heap table are 1 to 65,534, so no id is the entry of no
// owner or of a span.
static_assert(heap_id_of(HeapSlot{::foundation::core::StaticIndex<0>{}}) == HeapId{1});
static_assert(heap_id_of(HeapSlot{::foundation::core::StaticIndex<kHeapCount - 1>{}}) == HeapId{kLastHeapId});
static_assert(owner_entry_of(HeapId{kFirstHeapId}) != ChunkOwner::none);
static_assert(owner_entry_of(HeapId{kLastHeapId}) != ChunkOwner::span);
static_assert(heap_of(ChunkOwner::none).is_none());
static_assert(heap_of(ChunkOwner::span).is_none());
static_assert(heap_of(owner_entry_of(HeapId{7})).value_or(HeapId{1}) == HeapId{7});

// THE TAG BYTE: on aarch64 strip_tag removes bits 56 to 63, so a tagged
// address names the chunk of the same address with no tag.  On x86-64 it
// keeps each bit.  An address with no tag stays the same on each target.
namespace chunk_map_check {

inline constexpr std::uint64_t kUntagged = (std::uint64_t{0x7F12} << 32) | (std::uint64_t{7} << kChunkShift) | 4096;
inline constexpr std::uint64_t kTagged = (std::uint64_t{0x5A} << 56) | kUntagged;

}  // namespace chunk_map_check

static_assert(strip_tag(AddressWord{chunk_map_check::kUntagged}) == AddressWord{chunk_map_check::kUntagged});
#if defined(__aarch64__)
static_assert(strip_tag(AddressWord{chunk_map_check::kTagged}) == AddressWord{chunk_map_check::kUntagged});
static_assert(static_cast<std::uint64_t>(strip_tag(AddressWord{chunk_map_check::kTagged})) >> kChunkShift
              == chunk_map_check::kUntagged >> kChunkShift);
#else
static_assert(strip_tag(AddressWord{chunk_map_check::kTagged}) == AddressWord{chunk_map_check::kTagged});
#endif

// The last chunk of each extent is its guard.
static_assert(!is_guard_chunk(ChunkIndex{0}));
static_assert(is_guard_chunk(ChunkIndex{511}));
static_assert(!is_guard_chunk(ChunkIndex{512}));
static_assert(is_guard_chunk(ChunkIndex{1023}));

// The two bases and the two lengths of a record are four types, so the door
// that builds a record cannot swap two of them.  Each has the size of its
// word.
static_assert(!std::is_same_v<WindowBase, RegionBase> && !std::is_same_v<WindowLength, RegionLength>);
static_assert(sizeof(WindowBase) == 8 && sizeof(RegionBase) == 8);
static_assert(sizeof(WindowLength) == 8 && sizeof(RegionLength) == 8);
static_assert(!std::is_convertible_v<AddressWord, WindowBase> && !std::is_convertible_v<WindowBase, RegionBase>);

// The map is one word, and a record is five words.  Each copies as words
// with a trivial copy constructor, and its seal keeps it from a byte image.
static_assert(sizeof(BiasedChunkMap) == 8);
static_assert(BiasedChunkMap{}.is_empty());
static_assert(sizeof(WindowRecord) == 40);
static_assert(std::is_trivially_copy_constructible_v<WindowRecord> && !std::is_trivially_copyable_v<WindowRecord>);
static_assert(std::is_trivially_copy_constructible_v<BiasedChunkMap> && !std::is_trivially_copyable_v<BiasedChunkMap>);

// The empty record holds no address, and a new table holds the empty record
// in each slot.
static_assert(WindowRecord{}.is_empty());
static_assert(!WindowRecord{}.is_in_window(AddressWord{0}));
static_assert(!WindowRecord{}.is_in_window(AddressWord{4096}));

namespace chunk_map_check {

[[nodiscard]] consteval bool is_new_table_empty() noexcept {
    WindowTable const table{};
    bool is_empty = table.host().is_empty();
    is_empty = is_empty && table.record(WindowSlot{::foundation::core::StaticIndex<kWindowSlots - 1>{}}).is_empty();
    return is_empty;
}

}  // namespace chunk_map_check

static_assert(chunk_map_check::is_new_table_empty());
static_assert(!std::is_copy_constructible_v<WindowTable>);

}  // namespace foundation::alloc
