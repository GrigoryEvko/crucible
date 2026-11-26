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

// A list lends its elements only for a read: view(), the loop, get() and the
// iteration verbs give const elements, also on a list that is not const.
static_assert(std::is_same_v<decltype(std::declval<FixedList<int, 4>&>().view()), View<int const>>);
static_assert(std::is_same_v<decltype(std::declval<FixedList<int, 4>&>().begin()), ViewCursor<int const>>);
static_assert(std::is_same_v<decltype(*std::declval<FixedList<int, 4>&>().get(0).expect("")), int const&>);
static_assert(std::is_same_v<decltype(*std::declval<FixedList<int, 4>&>().reverse().begin()), int const&>);
static_assert(std::is_same_v<decltype(std::declval<FixedList<int, 4>&>().append(1)), Result<Index<4>, Full<int>>>);

// A linear element has no set, because set ends the old element, and it has
// exchange.  An element that does not copy has no update.
template <class List>
concept Sets = requires(List& list, Index<2> index, typename List::element_type value) {
    list.set(index, static_cast<typename List::element_type&&>(value));
};
template <class List>
concept Exchanges = requires(List& list, Index<2> index, typename List::element_type value) {
    static_cast<void>(list.exchange(index, static_cast<typename List::element_type&&>(value)));
};
template <class List>
concept Updates = requires(List& list, Index<2> index) { list.update(index, [](auto&) noexcept {}); };
static_assert(Sets<FixedList<int, 2>> && Sets<FixedList<Owner, 2>> && !Sets<FixedList<LinearToken, 2>>);
static_assert(Exchanges<FixedList<int, 2>> && Exchanges<FixedList<LinearToken, 2>>);
static_assert(Updates<FixedList<int, 2>> && !Updates<FixedList<LinearToken, 2>>);

// An append, a take, a view and a loop of a temporary list are refused.  A
// list has no subscript, no data() and no indices().  The verb that adds at
// the end is append: a list has no push.
template <class List>
concept AppendsToTemporary = requires { std::declval<List>().append(std::declval<typename List::element_type>()); };
template <class List>
concept HasPush = requires(List& list) { list.push(std::declval<typename List::element_type>()); };
template <class List>
concept TakesFromTemporary = requires { std::declval<List>().take_last(); };
template <class List>
concept ViewsTemporary = requires { std::declval<List>().view(); };
template <class List>
concept HasSubscript = requires(List& list) { list[0]; };
template <class List>
concept HasIndices = requires(List& list) { list.indices(); };
static_assert(!AppendsToTemporary<FixedList<int, 2>> && !TakesFromTemporary<FixedList<int, 2>>);
static_assert(!HasPush<FixedList<int, 2>>);
static_assert(!ViewsTemporary<FixedList<int, 2>> && !ViewsTemporary<FixedList<int, 2> const>);
static_assert(!HasSubscript<FixedList<int, 2>> && !HasIndices<FixedList<int, 2>>);

// Append, get, take and clear in a constant evaluation.  An append into a
// full list gives Full, and each read below the count sees the appended
// value.
[[nodiscard]] consteval int append_take_get() noexcept {
    FixedList<int, 3> list;
    int total = *list.get(list.append(1).expect("the list has room").raw()).expect("the list holds an element");
    static_cast<void>(list.append(2).expect("the list has room"));
    static_cast<void>(list.append(3).expect("the list has room"));
    bool const is_refused = list.append(4).is_err();
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
static_assert(append_take_get() == 1 + 2 + 3 + 2 + (1 + 2));

// The value doors in a constant evaluation, for an element with a
// user-provided end.  Each digit of the result is one element.
[[nodiscard]] consteval int insert_set_exchange_update_remove() noexcept {
    FixedList<Owner, 4> list;
    list.insert(index<0>(), Owner{3}).expect("the list has room");
    list.insert(index<0>(), Owner{1}).expect("the list has room");
    list.insert(index<1>(), Owner{2}).expect("the list has room");
    list.insert(index<3>(), Owner{4}).expect("the list has room");
    bool const is_refused = list.insert(index<0>(), Owner{9}).is_err();
    list.set(index<0>(), Owner{5});
    int const old = list.exchange(index<1>(), Owner{6}).value;
    list.update(index<2>(), [](Owner& element) noexcept { element.value += 4; });
    int const removed = list.remove(index<1>()).expect("the list holds an element").value;
    bool const is_past = list.remove(index<3>()).is_none();
    int digits = 0;
    for (Owner const& element : list) {
        digits = digits * 10 + element.value;
    }
    return is_refused && is_past ? digits * 100 + old * 10 + removed : -1;
}
static_assert(insert_set_exchange_update_remove() == 57426);

// The copy of a list of an element with a user-provided copy copies each
// element, and the move gives each element to the target and leaves the
// source empty.
[[nodiscard]] consteval int copy_and_move_each_element() noexcept {
    FixedList<Owner, 3> source;
    static_cast<void>(source.append(Owner{1}).expect("the list has room"));
    static_cast<void>(source.append(Owner{2}).expect("the list has room"));
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
    static_cast<void>(prefix.append(1.0).expect("the list has room"));
    FixedList<double, 3> longer;
    static_cast<void>(longer.append(1.0).expect("the list has room"));
    static_cast<void>(longer.append(2.0).expect("the list has room"));
    FixedList<double, 3> smaller_first;
    static_cast<void>(smaller_first.append(0.5).expect("the list has room"));
    static_cast<void>(smaller_first.append(9.0).expect("the list has room"));
    FixedList<double, 3> negative_zero;
    static_cast<void>(negative_zero.append(-0.0).expect("the list has room"));
    FixedList<double, 3> positive_zero;
    static_cast<void>(positive_zero.append(0.0).expect("the list has room"));
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

// A list of an element that is not trivially default constructible is a
// constant expression: the empty member of its slots is active, so a static
// list is constant-initialized, its bytes stay zero, and no constructor
// writes a page of its slots.  A constexpr variable asks for the same
// constant initialization as constinit, and for a constant end.  The two
// statics of crucible/SchemaTable.h are constinit lists of such an element.
inline constexpr FixedList<Owner, 4> kConstantListOfOwners{};
static_assert(kConstantListOfOwners.size() == 0);
static_assert(!std::is_trivially_default_constructible_v<Owner>);

// A constant evaluation appends into a list that starts with the empty member
// active, and reads each element.
[[nodiscard]] consteval int append_after_the_empty_member() noexcept {
    FixedList<Owner, 2> list{};
    static_cast<void>(list.append(Owner{5}).expect("the list has room"));
    static_cast<void>(list.append(Owner{6}).expect("the list has room"));
    int total = 0;
    for (Owner const& element : list) {
        total += element.value;
    }
    return total + static_cast<int>(list.take_last().expect("the list holds an element").value);
}
static_assert(append_after_the_empty_member() == 5 + 6 + 6);

// The drain ends the list that it holds, so its gate (LentArmStoresNoBorrow of
// foundation/core/Choice.h) refuses a body that takes an element by reference
// and can store a borrow outside the call, such as a body with a reference to
// an Option of a View.  It admits the same body with a reference to a count,
// and a body that takes the element by value gets a copy.  The fixture
// neg_core_fixed_list_drain_keeps_an_element shows the refusal of the door.
template <class Kept>
struct KeepsElementByReference final {
    Kept& kept;
    void operator()(Owner const& /*element*/) const noexcept {}
};
template <class Kept>
struct KeepsElementByValue final {
    Kept& kept;
    void operator()(Owner /*element*/) const noexcept {}
};
static_assert(!LentArmStoresNoBorrow<KeepsElementByReference<Option<View<int const>>>, Owner>);
static_assert(LentArmStoresNoBorrow<KeepsElementByReference<int>, Owner>
              && LentArmStoresNoBorrow<KeepsElementByValue<Option<View<int const>>>, Owner>);

}  // namespace detail::list_checks

}  // namespace foundation::core
