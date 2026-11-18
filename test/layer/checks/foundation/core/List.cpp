// The compile-time checks of foundation/core/List.h.

#include <foundation/core/List.h>

#include <foundation/ByteSeal.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::list_checks {

// An element whose copy and move are user-provided, so the list copies and
// moves each element itself.  A move leaves -1 in the source.
struct Owner final {
    int value = 0;
    constexpr Owner() noexcept = default;
    constexpr explicit Owner(int from) noexcept : value{from} {}
    constexpr Owner(Owner const& other) noexcept : value{other.value} {}
    constexpr Owner(Owner&& other) noexcept : value{other.value} { other.value = -1; }
    constexpr Owner& operator=(Owner const& other) noexcept {
        value = other.value;
        return *this;
    }
    constexpr Owner& operator=(Owner&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    constexpr ~Owner() {}
};

// A linear element: it moves, it does not copy, and the seal makes its
// assignment user-provided, so it is not trivially copyable, and it refuses
// a lifetime start over bytes.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr explicit LinearToken(int from) noexcept : id{from} {}
    constexpr LinearToken(LinearToken&& other) noexcept : id{other.id} {}
    constexpr LinearToken& operator=(LinearToken&& other) noexcept {
        id = other.id;
        return *this;
    }
    LinearToken(LinearToken const&) = delete;
};

// A list of a trivially copyable element holds its slots and its count, and
// copies its bytes.  A list of each other element copies each element.
static_assert(sizeof(FixedList<std::uint32_t, 4>) == 5 * sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<FixedList<std::uint64_t, 3>>);
static_assert(std::is_trivially_destructible_v<FixedList<int, 8>>);
static_assert(!std::is_trivially_copyable_v<FixedList<Owner, 2>>);
static_assert(std::is_nothrow_copy_constructible_v<FixedList<Owner, 2>>);
static_assert(std::is_nothrow_move_constructible_v<FixedList<Owner, 2>>);

// A list of a linear element is linear, it has no clear() and no copy, and
// its Full holds the value.  The Full of each other element is empty.
static_assert(::foundation::linear::is_linear_v<FixedList<LinearToken, 2>>);
static_assert(!::foundation::linear::is_linear_v<FixedList<int, 2>>);
static_assert(!std::is_copy_constructible_v<FixedList<LinearToken, 2>>);
static_assert(::foundation::linear::is_linear_v<Full<LinearToken>>);
static_assert(std::is_empty_v<Full<int>> && std::is_trivially_copyable_v<Full<int>>);

template <class List>
concept Clears = requires(List& list) { list.clear(); };
static_assert(Clears<FixedList<int, 2>> && !Clears<FixedList<LinearToken, 2>>);

// A push, a take, a view and a loop of a temporary list are refused.  A
// list has no subscript, no data() and no indices().
template <class List>
concept PushesTemporary = requires { std::declval<List>().push(std::declval<typename List::element_type>()); };
template <class List>
concept TakesFromTemporary = requires { std::declval<List>().take_last(); };
template <class List>
concept ViewsTemporary = requires { std::declval<List>().view(); };
template <class List>
concept HasSubscript = requires(List& list) { list[0]; };
template <class List>
concept HasIndices = requires(List& list) { list.indices(); };
static_assert(!PushesTemporary<FixedList<int, 2>> && !TakesFromTemporary<FixedList<int, 2>>);
static_assert(!ViewsTemporary<FixedList<int, 2>> && !ViewsTemporary<FixedList<int, 2> const>);
static_assert(!HasSubscript<FixedList<int, 2>> && !HasIndices<FixedList<int, 2>>);

// Push, get, take and clear in a constant evaluation.  A push into a full
// list gives Full, and each read below the count sees the pushed value.
[[nodiscard]] consteval int push_take_get() noexcept {
    FixedList<int, 3> list;
    int total = *list.push(1).expect("the list has room");
    static_cast<void>(list.push(2).expect("the list has room"));
    static_cast<void>(list.push(3).expect("the list has room"));
    bool const is_refused = list.push(4).is_err();
    total += *list.get(1).expect("1 is below the count");
    total += list.take_last().expect("the list holds an element");
    total += static_cast<int>(list.size());
    for (int const element : list) {
        total += element;
    }
    list.clear();
    bool const is_empty = list.size() == 0 && list.get(0).is_none() && list.take_last().is_none();
    return is_refused && is_empty ? total : -1;
}
static_assert(push_take_get() == 1 + 2 + 3 + 2 + (1 + 2));

// The copy of a list of an element with a user-provided copy copies each
// element, and the move gives each element to the target and leaves the
// source empty.
[[nodiscard]] consteval int copy_and_move_each_element() noexcept {
    FixedList<Owner, 3> source;
    static_cast<void>(source.push(Owner{1}).expect("the list has room"));
    static_cast<void>(source.push(Owner{2}).expect("the list has room"));
    FixedList<Owner, 3> const copied{source};
    FixedList<Owner, 3> const moved{std::move(source)};
    int total = 0;
    for (Owner const& element : copied) {
        total += element.value;
    }
    for (Owner const& element : moved) {
        total += element.value * 10;
    }
    return total + static_cast<int>(copied.size() + moved.size());
}
static_assert(copy_and_move_each_element() == 3 + 30 + 4);

// Two lists order as the first pair of elements that differ, and a prefix
// orders first.  A double element orders by the totalOrder of IEEE 754, so
// -0.0 orders before +0.0, and the two lists are not equal.
[[nodiscard]] consteval bool orders_by_elements() noexcept {
    FixedList<double, 3> prefix;
    static_cast<void>(prefix.push(1.0).expect("the list has room"));
    FixedList<double, 3> longer;
    static_cast<void>(longer.push(1.0).expect("the list has room"));
    static_cast<void>(longer.push(2.0).expect("the list has room"));
    FixedList<double, 3> smaller_first;
    static_cast<void>(smaller_first.push(0.5).expect("the list has room"));
    static_cast<void>(smaller_first.push(9.0).expect("the list has room"));
    FixedList<double, 3> negative_zero;
    static_cast<void>(negative_zero.push(-0.0).expect("the list has room"));
    FixedList<double, 3> positive_zero;
    static_cast<void>(positive_zero.push(0.0).expect("the list has room"));
    return prefix < longer && longer > prefix && smaller_first < prefix && negative_zero < positive_zero
        && negative_zero != positive_zero;
}
static_assert(orders_by_elements());

// An element with an equality and no order gives a list with an equality
// and no order.
struct EqualOnly final {
    int value = 0;
    [[nodiscard]] constexpr bool operator==(EqualOnly const&) const noexcept = default;
};
template <class List>
concept OrdersLists = requires(List const& list) { list <=> list; };
template <class List>
concept EqualsLists = requires(List const& list) { list == list; };
static_assert(OrdersLists<FixedList<int, 2>> && EqualsLists<FixedList<int, 2>>);
static_assert(EqualsLists<FixedList<EqualOnly, 2>> && !OrdersLists<FixedList<EqualOnly, 2>>);

}  // namespace detail::list_checks

}  // namespace foundation::core
