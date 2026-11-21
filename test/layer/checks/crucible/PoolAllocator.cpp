// The compile-time checks of crucible/PoolAllocator.h.

#include <crucible/PoolAllocator.h>

namespace crucible {

static_assert(sizeof(PoolAllocator) == 80,
              "PoolAllocator layout: AlignedBytes (24) + three HeapArray columns (16 each) + 2*u32");
static_assert(alignof(PoolAllocator) == 8);

// A view must not outlive the scope it was minted in, so storing one in a
// field would let it escape. This walks the struct, nested members included.
static_assert(::fixy::no_scoped_view_field_check<PoolAllocator>());

}  // namespace crucible
