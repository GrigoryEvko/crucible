// The compile-time checks of crucible/KernelCache.h.

#include <crucible/KernelCache.h>

#include <foundation/Linear.h>

#include <type_traits>
#include <utility>

namespace crucible::detail::kernel_cache_checks {

// The writer handle moves, and no assignment writes over the claim of a live
// handle.  The move and the destructor are trivial, so the Itanium ABI passes
// the handle in a register.  The move assignment of its Ref is user-provided,
// so GCC 16 counts the handle as not trivially copyable, and the handle keeps
// the shape of a linear class.  A compiler that counts it as trivially copyable
// fails the build here.  The handle is one pointer.
using Slot = KernelCache::KernelCacheSlot;
using Writer = Slot::WriterHandle;
static_assert(std::is_nothrow_move_constructible_v<Writer> && !std::is_move_assignable_v<Writer>);
static_assert(!std::is_copy_constructible_v<Writer> && !std::is_trivially_copyable_v<Writer>);
static_assert(std::is_trivially_move_constructible_v<Writer> && std::is_trivially_destructible_v<Writer>);
static_assert(::foundation::linear::is_linear_v<Writer>);
static_assert(sizeof(Writer) == sizeof(Slot*), "a WriterHandle holds the slot and nothing more");

// Only a claim gives a writer, and a writer publishes only as a value that the
// caller gives up.
static_assert(!std::is_default_constructible_v<Writer> && !std::is_constructible_v<Writer, Slot&>);
template <class Handle>
concept PublishesByName =
    requires(Handle& writer, RowHash row, KernelCache::KernelRef kernel) { writer.publish(row, kernel); };
template <class Handle>
concept PublishesByMove =
    requires(Handle& writer, RowHash row, KernelCache::KernelRef kernel) { std::move(writer).publish(row, kernel); };
static_assert(!PublishesByName<Writer> && PublishesByMove<Writer>);

}  // namespace crucible::detail::kernel_cache_checks
