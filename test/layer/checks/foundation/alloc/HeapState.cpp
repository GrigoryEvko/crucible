// The compile-time checks of foundation/alloc/HeapState.h.

#include <foundation/alloc/HeapState.h>

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Layout.h>

#include <cstddef>
#include <type_traits>

namespace foundation::alloc {

// A heap state is 30 cache lines: the map word at offset 0 and the own entry
// at offset 8, the shape of the free test of the design probe, then the
// facts of the window, the bins and the slab heaps, and the spare bytes to
// the end of the last line.
static_assert(sizeof(HeapState) == 1920 && alignof(HeapState) == 64);
static_assert(HeapState::has_probe_shape());

// A heap state lives at its place.
static_assert(!std::is_copy_constructible_v<HeapState> && !std::is_copy_assignable_v<HeapState>);

// A new state names no window and owns no chunk, so the sentinel starts with
// no map and the entry none (THE SENTINEL).
static_assert(HeapState{}.chunk_map().is_empty());
static_assert(HeapState{}.self() == ChunkOwner::none);
static_assert(std::is_trivially_destructible_v<HeapState>);

// The cell of a thread is one word (THE CELL).
static_assert(sizeof(tls_heap_) == 8);

// THE BINS: 128 addresses for each class to 128 bytes, 632 for the classes 9
// to 32, and 2 for each class of 10 KiB and more: 1,720 addresses.
static_assert(detail::kBinStacks.first[1] == 0 && detail::kBinStacks.first[9] == 8 * 128);
static_assert(detail::kBinStacks.first[33] - detail::kBinStacks.first[9] == 632);
static_assert(detail::kBinStacks.first[kSizeClassCount + 1] == 1720 && kBinStackBytes == 13760);

// The stacks of a heap start after its HeapState.
static_assert(static_cast<std::size_t>(bin_stacks_at(AddressWord{0})) == sizeof(HeapState));

// The classes with a slab are 1 to 52.
static_assert(is_slab_class(SizeClass{1}) && is_slab_class(SizeClass{52}));
static_assert(!is_slab_class(SizeClass{0}) && !is_slab_class(SizeClass{53}));

}  // namespace foundation::alloc
