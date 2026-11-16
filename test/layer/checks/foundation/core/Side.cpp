// The compile-time checks of foundation/core/Side.h.

#include <foundation/core/Side.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::side_checks {

struct First final {};
struct Second final {};
struct NotFinal {};
struct NotEmpty final {
    int value = 0;
};

// A payload of two words, and a payload that only moves.
struct Pair {
    std::uint64_t low = 0;
    std::uint32_t high = 0;
};
struct MoveOnly {
    std::uint64_t value = 0;
    constexpr MoveOnly() noexcept = default;
    constexpr explicit MoveOnly(std::uint64_t given) noexcept : value{given} {}
    MoveOnly(MoveOnly const&) = delete("the payload only moves");
    MoveOnly& operator=(MoveOnly const&) = delete("the payload only moves");
    constexpr MoveOnly(MoveOnly&&) noexcept = default;
    constexpr MoveOnly& operator=(MoveOnly&&) noexcept = default;
    constexpr ~MoveOnly() = default;
};

// A tag is an empty final class.
static_assert(SideTag<First> && SideTag<Second>);
static_assert(!SideTag<NotFinal> && !SideTag<NotEmpty> && !SideTag<int>);

// A payload is an object that moves with no throw.
static_assert(SidePayload<int> && SidePayload<Pair> && SidePayload<MoveOnly>);
static_assert(!SidePayload<int const> && !SidePayload<int&> && !SidePayload<int[2]>);

// Two tags give two types.
static_assert(!std::is_same_v<Side<First, int>, Side<Second, int>>);
static_assert(!std::is_convertible_v<Side<First, int>, Side<Second, int>>);
static_assert(!std::is_constructible_v<Side<Second, int>, Side<First, int>>);

// No aggregate, no implicit conversion in either direction, no comparison.
static_assert(!std::is_aggregate_v<Side<First, Pair>>);
static_assert(!std::is_convertible_v<int, Side<First, int>> && std::is_constructible_v<Side<First, int>, int>);
static_assert(!std::is_convertible_v<Side<First, int>, int> && !std::is_constructible_v<int, Side<First, int>>);
static_assert(!std::is_default_constructible_v<Side<First, int>>);
template <class T>
concept Comparable = requires(T const& left, T const& right) { left == right; };
template <class T>
concept Addable = requires(T const& left, T const& right) { left + right; };
static_assert(!Comparable<Side<First, int>> && !Addable<Side<First, int>>);

// The size, the alignment and the layout of the payload.
static_assert(sizeof(Side<First, Pair>) == sizeof(Pair) && alignof(Side<First, Pair>) == alignof(Pair));
static_assert(sizeof(Side<First, std::uint8_t>) == 1 && sizeof(Side<First, double>) == sizeof(double));
static_assert(std::is_trivially_copyable_v<Side<First, Pair>> && std::is_standard_layout_v<Side<First, Pair>>);
static_assert(std::is_trivially_destructible_v<Side<First, Pair>>);
static_assert(!std::is_copy_constructible_v<Side<First, MoveOnly>>
              && std::is_nothrow_move_constructible_v<Side<First, MoveOnly>>);

// value() of a named Side, and take() of an rvalue Side.
[[nodiscard]] consteval bool reads_and_takes() {
    Side<First, Pair> const pair = side_as<First>(Pair{7, 9});
    Side<Second, MoveOnly> only = side_as<Second>(MoveOnly{11});
    MoveOnly const taken = std::move(only).take();
    return pair.value().low == 7 && pair.value().high == 9 && taken.value == 11;
}
static_assert(reads_and_takes());
static_assert(std::is_same_v<decltype(side_as<First>(std::declval<Pair&>())), Side<First, Pair>>);
static_assert(std::is_same_v<decltype(side_as<First>(std::declval<Pair const&>())), Side<First, Pair>>);

// value() of a temporary does not compile.
template <class T>
concept ReadsTemporary = requires { std::declval<T&&>().value(); };
static_assert(!ReadsTemporary<Side<First, int>>);

}  // namespace detail::side_checks

}  // namespace foundation::core
