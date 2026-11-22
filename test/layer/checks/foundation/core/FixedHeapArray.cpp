// The compile-time checks of foundation/core/FixedHeapArray.h.

#include <foundation/core/FixedHeapArray.h>

#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::fixed_heap_array_checks {

// The array holds the pointer and the count of its storage, and the count of
// its elements is in its type.
static_assert(sizeof(FixedHeapArray<std::uint64_t, 1024>) == 2 * sizeof(std::size_t));
static_assert(FixedHeapArray<int, 7>::size() == 7);

// The array does not copy and does not move, so it holds its storage from
// its mint to its end.
static_assert(!std::is_copy_constructible_v<FixedHeapArray<int, 4>>);
static_assert(!std::is_move_constructible_v<FixedHeapArray<int, 4>>);
static_assert(!std::is_copy_assignable_v<FixedHeapArray<int, 4>>);
static_assert(!std::is_move_assignable_v<FixedHeapArray<int, 4>>);

// The gates: no zero count, no const element, no alignment that is not a
// power of two, and no alignment below the alignment of the element.
template <class T, std::size_t N, std::size_t Align = alignof(T)>
concept HasFixedHeapArray = requires { typename FixedHeapArray<T, N, Align>; };
static_assert(HasFixedHeapArray<int, 1> && HasFixedHeapArray<std::uint8_t, 16, 64>);
static_assert(!HasFixedHeapArray<int, 0>);
static_assert(!HasFixedHeapArray<int const, 2>);
static_assert(!HasFixedHeapArray<int, 2, 12>);
static_assert(!HasFixedHeapArray<std::uint64_t, 2, 4>);

// The mint takes the allocation capability, and no other tag.
static_assert(CanMintFixedHeapArray<int, 4, alignof(int), ::foundation::effects::Alloc>);
static_assert(!CanMintFixedHeapArray<int, 4, alignof(int), ::foundation::effects::IO>);

// The reads give const elements and a View of the fixed extent.
static_assert(std::is_same_v<decltype(std::declval<FixedHeapArray<int, 8> const&>().view()), View<int const, 8>>);
static_assert(std::is_same_v<decltype(std::declval<FixedHeapArray<int, 8>&>().view()), View<int const, 8>>);
static_assert(std::is_same_v<decltype(std::declval<FixedHeapArray<int, 8>&>().at(index<3>())), int const&>);

}  // namespace detail::fixed_heap_array_checks

}  // namespace foundation::core
