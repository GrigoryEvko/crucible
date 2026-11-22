// The compile-time checks of foundation/core/FixedHeapArray.h.

#include <foundation/core/FixedHeapArray.h>

#include <foundation/core/Ref.h>
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

// A keyed array has the size of the array with no key, it carries the mark of
// a keyed owner, and it opens a frame only with a key of its brand.
struct KeyedCheckBrand final {};
using KeyedWords = FixedHeapArray<int, 8, alignof(int), KeyedCheckBrand>;
inline constexpr auto touch_run = [](auto run) noexcept { static_cast<void>(run.size()); };
using TouchRun = decltype(touch_run);

// A body that reaches the key of the brand through a reference.  The checks
// read its type only, and no code builds one.
struct KeyReachingRun {
    LoanKey<KeyedCheckBrand>& key;
    void operator()(auto run) const noexcept { static_cast<void>(run.size()); }
};

template <class Array, class Body>
concept OpensWithNoKey = requires(Array& array, Body const& body) { array.with_mut(body); };
template <class Array, class Key, class Body>
concept OpensWithKey = requires(Array& array, Key& key, Body const& body) { array.with_mut(key, body); };

static_assert(sizeof(KeyedWords) == sizeof(FixedHeapArray<int, 8>));
static_assert(::foundation::linear::carries_annotation(std::meta::dealias(^^KeyedWords), ^^loan_key_mark));
static_assert(!::foundation::linear::carries_annotation(^^FixedHeapArray<int, 8>, ^^loan_key_mark));
static_assert(OpensWithKey<KeyedWords, LoanKey<KeyedCheckBrand>, TouchRun>);
static_assert(!OpensWithNoKey<KeyedWords, TouchRun>);
static_assert(!OpensWithKey<KeyedWords, LoanKey<KeyedCheckBrand>, KeyReachingRun>);
static_assert(OpensWithNoKey<FixedHeapArray<int, 8>, TouchRun>);
static_assert(!OpensWithKey<FixedHeapArray<int, 8>, LoanKey<KeyedCheckBrand>, TouchRun>);
static_assert(std::is_same_v<decltype(std::declval<KeyedWords&>().with_mut(std::declval<LoanKey<KeyedCheckBrand>>(),
                                                                           std::declval<TouchRun const&>())),
                             LoanKey<KeyedCheckBrand>>);

}  // namespace detail::fixed_heap_array_checks

}  // namespace foundation::core
