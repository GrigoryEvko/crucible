// The compile-time checks of crucible/KernelCache.h.

#include <crucible/KernelCache.h>

#include <foundation/Linear.h>

#include <type_traits>

namespace crucible::detail::kernel_cache_checks {

// The writer handle moves, and no assignment writes over the writer token of
// a live handle.  The move and the destructor are trivial, so the Itanium ABI
// passes the handle in a register.  The move assignment of the token is
// user-provided, so GCC 16 counts the handle as not trivially copyable, and
// the handle keeps the shape of a linear class.  A compiler that counts it as
// trivially copyable fails the build here.  The handle takes the brand of its
// token, and each fact holds for each brand, so an empty class of this file
// stands in for one.  The token is empty, so the handle is one pointer.
struct WriterBrand {};
using Writer = KernelCache::KernelCacheSlot::WriterHandle<WriterBrand>;
static_assert(std::is_nothrow_move_constructible_v<Writer> && !std::is_move_assignable_v<Writer>);
static_assert(!std::is_copy_constructible_v<Writer> && !std::is_trivially_copyable_v<Writer>);
static_assert(std::is_trivially_move_constructible_v<Writer> && std::is_trivially_destructible_v<Writer>);
static_assert(::foundation::linear::is_linear_v<Writer>);
static_assert(sizeof(Writer) == sizeof(KernelCache::KernelCacheSlot*),
              "KernelCacheSlot::WriterHandle must EBO-collapse its Permission.");

}  // namespace crucible::detail::kernel_cache_checks
