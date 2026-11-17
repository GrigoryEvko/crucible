// The compile-time checks of foundation/core/SmallList.h.

#include <foundation/core/SmallList.h>

#include <foundation/core/Id.h>
#include <foundation/core/Verbs.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::small_list_checks {

struct OpTag final {};

// An element with a copy of its own: the core would copy its bytes, so the
// list refuses it.
struct CountedCopy final {
    int value = 0;
    constexpr CountedCopy() noexcept = default;
    constexpr CountedCopy(CountedCopy const& other) noexcept : value{other.value + 1} {}
};

// An element of an alignment of 16 would need padding in the state.
struct alignas(16) Wide final {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

using Maximum = Quantity<unit::Element, std::size_t>;

// The state holds the inline slots rounded up to a word, a pointer, three
// counts, and a word of the lend count and its padding, with no other byte.
// The list moves and does not copy, and its state is an owner state of the
// core.
static_assert(sizeof(SmallList<std::uint32_t, 3>) == 16 + 5 * sizeof(std::size_t));
static_assert(sizeof(SmallList<std::uint64_t, 4>) == 32 + 5 * sizeof(std::size_t));
static_assert(sizeof(SmallList<std::uint8_t, 1>) == 8 + 5 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<SmallList<int, 4>>
              && std::is_nothrow_move_assignable_v<SmallList<int, 4>>);
static_assert(!std::is_copy_constructible_v<SmallList<int, 4>> && !std::is_copy_assignable_v<SmallList<int, 4>>);
static_assert(!std::is_default_constructible_v<SmallList<int, 4>>);
static_assert(OwnerState<owner::SmallListState<int, 4>>
              && OwnerState<owner::SmallListState<Id<OpTag, std::uint32_t>, 3>>);

// The element is bytewise and of an alignment of at most 8.  An element with
// no default constructor passes.  The seal of an Index makes its assignment
// user-provided, so an Index is no bytewise element.
static_assert(SmallListElement<int> && SmallListElement<Id<OpTag, std::uint32_t>> && !SmallListElement<Index<8>>);
static_assert(!SmallListElement<CountedCopy> && !SmallListElement<Wide> && !SmallListElement<int const>);
static_assert(!SmallListElement<Box<int>> && !SmallListElement<GrowList<int>>);
static_assert(SmallListInline<int, 1> && !SmallListInline<int, 0>);

// An append takes the allocation capability first.  A list gives no View, no
// get, no cursor and no subscript out of a frame, and a temporary list lends
// nothing.
template <class List, class Marker>
concept Appends = requires(List& list, Marker marker) { list.append(marker, 1); };
struct OtherMarker final {};
static_assert(Appends<SmallList<int, 4>, Alloc> && !Appends<SmallList<int, 4>, OtherMarker>);
static_assert(!Appends<SmallList<int, 4> const, Alloc>);
template <class List>
concept Views = requires(List& list) { list.view(); };
template <class List>
concept Reads = requires(List& list) { list.get(0); };
template <class List>
concept Loops = requires(List& list) { list.begin(); };
template <class List>
concept HasSubscript = requires(List& list) { list[0]; };
static_assert(!Views<SmallList<int, 4>> && !Reads<SmallList<int, 4>> && !Loops<SmallList<int, 4>>);
static_assert(!Views<SmallList<int, 4> const> && !Reads<SmallList<int, 4> const> && !HasSubscript<SmallList<int, 4>>);

// A frame door gives the result of its body when that result holds no
// borrow.
template <class List, class Body>
concept Lends = requires(List&& list, Body body) { static_cast<List&&>(list).with_view(body); };
struct CountsElements final {
    [[nodiscard]] constexpr std::size_t operator()(View<int const> view) const noexcept { return view.size(); }
};
struct GivesElements final {
    [[nodiscard]] constexpr View<int const> operator()(View<int const> view) const noexcept { return view; }
};
static_assert(Lends<SmallList<int, 4>&, CountsElements> && Lends<SmallList<int, 4> const&, CountsElements>);
static_assert(!Lends<SmallList<int, 4>&, GivesElements> && !Lends<SmallList<int, 4>, CountsElements>);

// The surface of the list is closed over the shared verbs and the names of
// the family.
inline constexpr TextView small_list_names{"element_type take_last is_empty clear with_view"};
CRUCIBLE_CLOSED_SURFACE(small_list_names, SmallList<int, 4>);
CRUCIBLE_CLOSED_SURFACE(small_list_names, SmallList<Id<OpTag, std::uint32_t>, 1>);

// The sum of the elements of a list, through its frame door.
template <class T, std::size_t Inline>
[[nodiscard]] constexpr std::uint64_t sum_of(SmallList<T, Inline> const& list) noexcept {
    return list.with_view([](View<T const> run) noexcept {
        std::uint64_t sum = 0;
        for (T const& element : run) {
            sum += static_cast<std::uint64_t>(element.raw());
        }
        return sum;
    });
}

// Inline appends in a constant evaluation.  The allocation door is no
// constexpr function, so the evaluation proves that no append below Inline
// elements reaches it.  A take, a clear, a move and a frame door run there
// too.
[[nodiscard]] consteval bool inline_appends_allocate_nothing() noexcept {
    Alloc const alloc = ::foundation::effects::testing::test().alloc;
    using Op = Id<OpTag, std::uint32_t>;
    SmallList<Op, 4> list = mint_small_list<Op, 4>(Maximum{8});
    for (std::uint32_t value = 1; value <= 4; ++value) {
        list.append(alloc, Op{value}).expect("the list is below its maximum");
    }
    bool const is_full_inline = list.size() == 4 && sum_of(list) == 10;
    Op const last = list.take_last().expect("the list holds four elements");
    SmallList<Op, 4> moved{std::move(list)};
    bool const is_moved = moved.size() == 3 && sum_of(moved) == 6 && last.raw() == 4;
    moved.clear();
    SmallList<Op, 4> capped = mint_small_list<Op, 4>(Maximum{1});
    capped.append(alloc, Op{7}).expect("the list is below its maximum");
    bool const is_capped = capped.append(alloc, Op{8}).is_err() && capped.size() == 1;
    return is_full_inline && is_moved && moved.is_empty() && is_capped && moved.take_last().is_none();
}
static_assert(inline_appends_allocate_nothing());

}  // namespace detail::small_list_checks

}  // namespace foundation::core
