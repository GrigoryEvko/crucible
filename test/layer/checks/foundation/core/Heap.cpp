// The compile-time checks of foundation/core/Heap.h.

#include <foundation/core/Heap.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::heap_checks {

// A marker of a test file with the member that the structural gate of an
// earlier tree read.  It is not the tag, so it does not pass.
struct HomemadeMarker {
    using grants_allocation = void;
};

struct NoMarker {};

// The allocation tag is empty and costs nothing.  It is an authority, so no
// code outside its owner builds one: the default constructor is private.
// No route builds one with no constructor either: the tag is not trivially
// copyable, so std::bit_cast refuses it, and it is not an implicit-lifetime
// type, so std::start_lifetime_as refuses it.  A holder copies it.
static_assert(std::is_empty_v<Alloc>);
static_assert(!std::is_default_constructible_v<Alloc>);
static_assert(!std::is_trivially_copyable_v<Alloc> && !std::is_implicit_lifetime_v<Alloc>);
static_assert(std::is_nothrow_copy_constructible_v<Alloc> && std::is_trivially_destructible_v<Alloc>);

// The gate takes the tag and no other type.
static_assert(AllocationCapability<Alloc>);
static_assert(!AllocationCapability<HomemadeMarker>);
static_assert(!AllocationCapability<NoMarker>);
static_assert(!AllocationCapability<Alloc const>);
static_assert(!AllocationCapability<Alloc&>);
static_assert(!AllocationCapability<int>);

// The door takes one Layout, and it gives a pointer that is never null.  The
// release takes the same layout.  No door takes a count of bytes and an
// alignment as two numbers, so the two cannot swap at a call.
static_assert(std::is_same_v<decltype(heap_allocate_(::foundation::alloc::Layout::of<int>())), void*>);
static_assert(noexcept(heap_allocate_(::foundation::alloc::Layout::of<int>())));
static_assert(std::is_same_v<decltype(heap_allocate_zeroed_(::foundation::alloc::Layout::of<int>())), void*>);
static_assert(std::is_same_v<decltype(heap_release_(nullptr, ::foundation::alloc::Layout::of<int>())), void>);
static_assert(noexcept(heap_release_(nullptr, ::foundation::alloc::Layout::of<int>())));
static_assert(!std::is_invocable_v<decltype(&heap_allocate_), std::size_t>);
static_assert(!std::is_invocable_v<decltype(&heap_allocate_zeroed_), std::size_t>);

// The layout of count slots holds their bytes at the alignment of a slot.
static_assert(slots_layout_<std::uint64_t>(3).bytes().raw() == 24);
static_assert(slots_layout_<std::uint64_t>(3).alignment() == ::foundation::alloc::Alignment{8});

}  // namespace detail::heap_checks

}  // namespace foundation::core
