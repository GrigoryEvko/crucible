// The compile-time checks of crucible/KernelCache.h.

#include <crucible/KernelCache.h>

#include <type_traits>

namespace crucible::detail::kernel_cache_checks {

// The writer handle moves, and no assignment writes over the writer token of
// a live handle.  The move is user-provided, so the handle is not trivially
// copyable, and it keeps the shape of a linear class.
using Writer = KernelCache::KernelCacheSlot::WriterHandle;
static_assert(std::is_nothrow_move_constructible_v<Writer> && !std::is_move_assignable_v<Writer>);
static_assert(!std::is_copy_constructible_v<Writer> && !std::is_trivially_copyable_v<Writer>);

}  // namespace crucible::detail::kernel_cache_checks
