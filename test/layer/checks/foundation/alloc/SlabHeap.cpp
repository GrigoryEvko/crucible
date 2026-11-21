// The compile-time checks of foundation/alloc/SlabHeap.h.

#include <foundation/alloc/SlabHeap.h>

#include <foundation/alloc/Metadata.h>

#include <type_traits>

namespace foundation::alloc {

// The root of a slab heap is one link, so a HeapState holds one root for each
// class in four bytes.  A new heap is empty.
static_assert(sizeof(SlabHeap) == 4);
static_assert(SlabHeap{}.is_empty() && SlabHeap{}.min() == SlabLink::none);

// The records of a window are one word, which a call passes in a register:
// the copy and the destructor are trivial.  The assignments are not, so
// std::bit_cast builds no records from bytes.
static_assert(sizeof(SlabRecords) == 8 && std::is_trivially_copy_constructible_v<SlabRecords>);
static_assert(std::is_trivially_destructible_v<SlabRecords> && !std::is_trivially_copyable_v<SlabRecords>);
static_assert(SlabRecords{}.is_empty());

}  // namespace foundation::alloc
