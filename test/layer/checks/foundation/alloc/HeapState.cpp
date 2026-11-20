// The compile-time checks of foundation/alloc/HeapState.h.

#include <foundation/alloc/HeapState.h>

#include <foundation/alloc/ChunkMap.h>

#include <type_traits>

namespace foundation::alloc {

// A heap state is one cache line, with the map word at offset 0 and the own
// entry at offset 8, the shape of the free test of the design probe.
static_assert(sizeof(HeapState) == 64 && alignof(HeapState) == 64);
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

}  // namespace foundation::alloc
