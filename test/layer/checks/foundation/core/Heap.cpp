// The compile-time checks of foundation/core/Heap.h.

#include <foundation/core/Heap.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail::heap_checks {

// A marker of a test file with the member that the structural gate of an
// earlier tree read.  It is not the tag, so it does not pass.
struct HomemadeMarker {
    using grants_allocation = void;
};

struct NoMarker {};

// The allocation tag is empty and costs nothing.
static_assert(std::is_empty_v<Alloc>);
static_assert(std::is_trivially_copyable_v<Alloc>);
static_assert(std::is_nothrow_default_constructible_v<Alloc>);

// The gate takes the tag and no other type.
static_assert(AllocationCapability<Alloc>);
static_assert(!AllocationCapability<HomemadeMarker>);
static_assert(!AllocationCapability<NoMarker>);
static_assert(!AllocationCapability<Alloc const>);
static_assert(!AllocationCapability<Alloc&>);
static_assert(!AllocationCapability<int>);

// The door takes the bytes and the alignment, and it gives a pointer that
// is never null.  The release takes the same two values.
static_assert(std::is_same_v<decltype(heap_allocate_(std::size_t{1}, std::size_t{1})), void*>);
static_assert(noexcept(heap_allocate_(std::size_t{1}, std::size_t{1})));
static_assert(std::is_same_v<decltype(heap_release_(nullptr, std::size_t{1}, std::size_t{1})), void>);
static_assert(noexcept(heap_release_(nullptr, std::size_t{1}, std::size_t{1})));

}  // namespace detail::heap_checks

}  // namespace foundation::core
