// The compile-time checks of foundation/core/Array.h.

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::array_checks {

struct NoDefault {
    int value;
    constexpr explicit NoDefault(int from) noexcept : value{from} {}
    [[nodiscard]] friend constexpr bool operator==(NoDefault, NoDefault) noexcept = default;
};

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// An array holds its elements and nothing else, and it copies as its
// elements do.
static_assert(sizeof(FixedArray<std::uint32_t, 4>) == 4 * sizeof(std::uint32_t));
static_assert(sizeof(FixedArray<Record, 3>) == 3 * sizeof(Record));
static_assert(std::is_trivially_copyable_v<FixedArray<std::uint64_t, 2>>);
static_assert(std::is_trivially_destructible_v<FixedArray<Record, 3>>);
static_assert(FixedArray<int, 7>::size() == 7);

// The gates: no zero count, no array, no const and no reference element.
template <class T, std::size_t N>
concept HasFixedArray = requires { typename FixedArray<T, N>; };
static_assert(HasFixedArray<int, 1> && HasFixedArray<NoDefault, 2>);
static_assert(!HasFixedArray<int, 0>);
static_assert(!HasFixedArray<int const, 2>);
static_assert(!HasFixedArray<int[2], 2>);
static_assert(!HasFixedArray<int&, 2>);

// The default constructor value-initializes each element.  The list
// constructor takes exactly N arguments, and its form of one argument is
// explicit.
static_assert(std::is_default_constructible_v<FixedArray<int, 4>>);
static_assert(!std::is_default_constructible_v<FixedArray<NoDefault, 2>>);
static_assert(std::is_constructible_v<FixedArray<int, 3>, int, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int>);
static_assert(!std::is_constructible_v<FixedArray<int, 3>, int, int, int, int>);
static_assert(std::is_constructible_v<FixedArray<int, 1>, int>);
static_assert(!std::is_convertible_v<int, FixedArray<int, 1>>);
static_assert(std::is_constructible_v<FixedArray<NoDefault, 2>, NoDefault, NoDefault>);

// No subscript, no pointer and no data() reach an element.
template <class Array>
concept HasSubscript = requires(Array array) { array[0]; };
template <class Array>
concept HasData = requires(Array array) { array.data(); };
static_assert(!HasSubscript<FixedArray<int, 4>&> && !HasData<FixedArray<int, 4>&>);
static_assert(!std::is_convertible_v<FixedArray<int, 4>&, int*>);

// view() of an lvalue gives a View of the fixed extent.  A view, a cursor
// and an element of a temporary are refused.
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4>&>().view()), View<int, 4>>);
static_assert(std::is_same_v<decltype(std::declval<FixedArray<int, 4> const&>().view()), View<int const, 4>>);
template <class Array>
concept ViewsTemporary = requires { std::declval<Array>().view(); };
template <class Array>
concept LoopsOverTemporary = requires { std::declval<Array>().begin(); };
template <class Array>
concept ReadsTemporary = requires { std::declval<Array>().at(std::declval<Index<4>>()); };
static_assert(!ViewsTemporary<FixedArray<int, 4>> && !ViewsTemporary<FixedArray<int, 4> const>);
static_assert(!LoopsOverTemporary<FixedArray<int, 4>> && !ReadsTemporary<FixedArray<int, 4>>);

// at takes an Index<N> or a constant index below N, and an Index of a
// different count does not convert down.
template <class Array, class Proof>
concept ReadsAt = requires(Array& array, Proof proof) { array.at(proof); };
static_assert(ReadsAt<FixedArray<int, 4>, Index<4>> && ReadsAt<FixedArray<int, 4>, StaticIndex<3>>);
static_assert(ReadsAt<FixedArray<int, 4>, Index<2>>);
static_assert(!ReadsAt<FixedArray<int, 4>, StaticIndex<4>>);
static_assert(!ReadsAt<FixedArray<int, 4>, Index<8>>);
static_assert(!ReadsAt<FixedArray<int, 4>, std::size_t>);

// The elements in a constant evaluation.
[[nodiscard]] consteval int sum_of(FixedArray<int, 4> const& array) noexcept {
    int total = 0;
    for (int const value : array) {
        total += value;
    }
    return total;
}

static_assert(sum_of(FixedArray<int, 4>{}) == 0);
static_assert(sum_of(FixedArray<int, 4>{1, 2, 3, 4}) == 10);
static_assert(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 6});
static_assert(!(FixedArray<int, 3>{4, 5, 6} == FixedArray<int, 3>{4, 5, 7}));
static_assert(FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}}
              == FixedArray<NoDefault, 2>{NoDefault{1}, NoDefault{2}});

[[nodiscard]] consteval int write_and_read() noexcept {
    FixedArray<int, 3> array{};
    array.at(index<0>()) = 10;
    array.at(Index<3>::check(2).expect("2 is below 3")) = 30;
    fill(array.view().window(1, 1).expect("one element fits"), 20);
    int total = 0;
    for (Index<3> const position : array.indices()) {
        total += array.at(position) * static_cast<int>(position.raw() + 1);
    }
    return total;
}
static_assert(write_and_read() == 10 * 1 + 20 * 2 + 30 * 3);

// The View of an array reads each element at its proven index.
[[nodiscard]] consteval int view_reads() noexcept {
    FixedArray<int, 4> array{7, 8, 9, 10};
    View<int const, 4> const elements = array.view();
    int total = 0;
    for (Index<4> const position : elements.indices()) {
        total += elements.at(position);
    }
    return total + elements.at(index<3>());
}
static_assert(view_reads() == 7 + 8 + 9 + 10 + 10);

// get reads below the count and gives no value at the count, in a constant
// evaluation too.
[[nodiscard]] consteval int get_reads() noexcept {
    FixedArray<int, 3> array{5, 6, 7};
    int total = *array.get(2).expect("2 is below 3");
    *array.get(0).expect("0 is below 3") = 1;
    total += array.at(index<0>());
    return array.get(3).is_none() ? total : -1;
}
static_assert(get_reads() == 8);

// enumerate() gives a Record of the proven index and a Ref to the element.
// Only a View of a fixed extent and a FixedArray have indices(), so only
// they have enumerate().  The loop of a temporary array is refused.
template <class Elements>
concept Enumerates = requires(Elements& elements) { elements.enumerate(); };
template <class Array>
concept EnumeratesTemporary = requires { std::declval<Array>().enumerate(); };
static_assert(Enumerates<View<int, 4>> && Enumerates<FixedArray<int, 4>> && Enumerates<FixedArray<int, 4> const>);
static_assert(!Enumerates<View<int>> && !Enumerates<HeapArray<int>>);
static_assert(!EnumeratesTemporary<FixedArray<int, 4>> && !EnumeratesTemporary<FixedArray<int, 4> const>);

// The target of a Ref of each brand.  Each call of enumerate() or zip gives
// its Refs a fresh brand, so a check reads the parts of a pair and the
// target of each Ref, and never names the brand.
template <class R>
struct RefTargetOf;
template <class T, class Brand>
struct RefTargetOf<Ref<T, Brand>> {
    using type = T;
};
template <class R>
using ref_target_t = typename RefTargetOf<R>::type;

// The aliases are local to the body, so no fresh brand leaves the scope of
// its call.  zip of two Views of one fixed extent gives the loop, and a pair
// holds two pointers.  When a View has a dynamic extent, zip gives a Result
// (enumerate_and_zip).  The fixture neg_core_zip_different_fixed_extents
// holds the refusal of two different fixed extents, because a concept that
// reads the function template zip is an open read.
[[nodiscard]] consteval bool pairs_hold_an_index_and_refs() noexcept {
    using ConstEnumeratedPair = decltype(*std::declval<FixedArray<int, 4> const&>().enumerate().begin());
    using EnumeratedPair = decltype(*std::declval<View<int, 4>>().enumerate().begin());
    using ZippedPair = decltype(*zip(std::declval<View<int, 4>>(), std::declval<View<long const, 4>>()).begin());
    return std::is_same_v<pack_at_t<0, typename ConstEnumeratedPair::types>, Index<4>>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename ConstEnumeratedPair::types>>, int const>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename EnumeratedPair::types>>, int>
        && std::is_same_v<ref_target_t<pack_at_t<0, typename ZippedPair::types>>, int>
        && std::is_same_v<ref_target_t<pack_at_t<1, typename ZippedPair::types>>, long const>
        && sizeof(ZippedPair) == 2 * sizeof(void*);
}
static_assert(pairs_hold_an_index_and_refs());

// enumerate and zip in a constant evaluation.
[[nodiscard]] consteval int enumerate_and_zip() noexcept {
    FixedArray<int, 4> const weights{1, 2, 3, 4};
    FixedArray<int, 4> values{};
    for (auto const [position, element] : values.enumerate()) {
        *element = static_cast<int>(position.raw()) + 10;
    }
    int total = 0;
    for (auto const [weight, value] : zip(weights.view(), values.view())) {
        total += *weight * *value;
    }
    View<int const> const head = values.view().window(0, 2).expect("two elements fit");
    return total + (zip(head, weights.view()).is_err() ? 1 : 0);
}
static_assert(enumerate_and_zip() == 1 * 10 + 2 * 11 + 3 * 12 + 4 * 13 + 1);

// Two calls of enumerate() or zip give two fresh brands, so a Ref of one
// loop is not a Ref of the other loop.
[[nodiscard]] consteval bool two_calls_give_two_brands() noexcept {
    FixedArray<int, 2> const values{1, 2};
    auto const first_loop = values.enumerate();
    auto const second_loop = values.enumerate();
    auto const first_pairs = zip(values.view(), values.view());
    auto const second_pairs = zip(values.view(), values.view());
    return !std::same_as<decltype(first_loop), decltype(second_loop)>
        && !std::same_as<decltype(first_pairs), decltype(second_pairs)>;
}
static_assert(two_calls_give_two_brands());

// A HeapArray holds a pointer and a count.  It moves, it does not copy, and
// the allocation capability is the first argument of its one door.
struct OtherTag final {
    using grants_allocation = void;
};
struct MoveOnlyElement final {
    MoveOnlyElement() noexcept = default;
    MoveOnlyElement(MoveOnlyElement&&) noexcept = default;
    MoveOnlyElement(MoveOnlyElement const&) = delete;
};
static_assert(sizeof(HeapArray<std::uint32_t>) == 2 * sizeof(std::size_t));
static_assert(sizeof(HeapArray<std::uint8_t, 4096>) == 2 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<HeapArray<int>>
              && std::is_nothrow_move_assignable_v<HeapArray<int>>);
static_assert(!std::is_copy_constructible_v<HeapArray<int>> && !std::is_copy_assignable_v<HeapArray<int>>);
static_assert(!std::is_trivially_copyable_v<HeapArray<int>>);
static_assert(CanMintHeapArray<int, alignof(int), Alloc>);
static_assert(!CanMintHeapArray<int, alignof(int), OtherTag>);
static_assert(!CanMintHeapArray<MoveOnlyElement, alignof(MoveOnlyElement), Alloc>);
static_assert(!CanMintHeapArray<int, 3, Alloc>);
static_assert(!CanMintHeapArray<std::uint64_t, 4, Alloc>);
static_assert(!::foundation::linear::is_linear_v<HeapArray<int>>);

// A HeapArray of a linear element is linear, so each path of its holder
// consumes it, and its destructor is no silent drop of the elements.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    LinearToken() noexcept = default;
    LinearToken(LinearToken&&) noexcept = default;
    LinearToken& operator=(LinearToken&&) noexcept { return *this; }
    LinearToken(LinearToken const&) = delete;
};
static_assert(::foundation::linear::is_linear_v<HeapArray<LinearToken>>);

static_assert(ViewsTemporary<HeapArray<int>&> && !ViewsTemporary<HeapArray<int>>);
static_assert(!ViewsTemporary<HeapArray<int> const>);

// The empty array holds no storage, in a constant evaluation too.
[[nodiscard]] consteval std::size_t empty_reads() noexcept {
    HeapArray<int> const empty{};
    std::size_t turns = 0;
    for (int const element : empty) {
        turns += static_cast<std::size_t>(element) + 1;
    }
    return empty.size() + turns + (empty.get(0).is_none() ? 0 : 1);
}
static_assert(empty_reads() == 0);

}  // namespace detail::array_checks

}  // namespace foundation::core
