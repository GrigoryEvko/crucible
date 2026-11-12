// The compile-time checks of foundation/core/Heap.h.

#include <foundation/core/Heap.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail::heap_checks {

// A stand-in for the allocation tag of foundation/effects, which is above
// this layer.
struct TestAllocation {
    using grants_allocation = void;
};

struct NoMarker {};

struct MarkerWithState {
    using grants_allocation = void;
    int state = 0;
};

// The allocation tag.
static_assert(AllocationCapability<TestAllocation>);
static_assert(!AllocationCapability<NoMarker>);
static_assert(!AllocationCapability<MarkerWithState>);
static_assert(!AllocationCapability<int>);

// The door takes the bytes and the alignment, and it gives a pointer that
// is never null.  The release takes the same two values.
static_assert(std::is_same_v<decltype(heap_allocate_(std::size_t{1}, std::size_t{1})), void*>);
static_assert(noexcept(heap_allocate_(std::size_t{1}, std::size_t{1})));
static_assert(std::is_same_v<decltype(heap_release_(nullptr, std::size_t{1}, std::size_t{1})), void>);
static_assert(noexcept(heap_release_(nullptr, std::size_t{1}, std::size_t{1})));

}  // namespace detail::heap_checks

}  // namespace foundation::core
