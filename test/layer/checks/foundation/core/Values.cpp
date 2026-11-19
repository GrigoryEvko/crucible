// The compile-time checks of foundation/core/Values.h.

#include <foundation/core/Values.h>

#include <foundation/Pinned.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core::detail::values_checks {

// A class that only moves, and a class that never moves.
class OnlyMoves {
    int value_ = 0;

public:
    constexpr explicit OnlyMoves(int value) noexcept : value_{value} {}
    constexpr OnlyMoves(OnlyMoves&&) noexcept = default;
    constexpr OnlyMoves& operator=(OnlyMoves&&) noexcept = default;
    OnlyMoves(OnlyMoves const&) = delete("a value of this class only moves");
    OnlyMoves& operator=(OnlyMoves const&) = delete("a value of this class only moves");
    [[nodiscard]] constexpr int value() const noexcept { return value_; }
};

struct Anchored : ::foundation::Pinned<Anchored> {
    int value = 0;
};

// A move that can throw.
struct ThrowingMove {
    ThrowingMove(ThrowingMove&&) noexcept(false);
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false);
};

static_assert(Exchangeable<int> && Exchangeable<double> && Exchangeable<OnlyMoves> && Exchangeable<int*>);
static_assert(!Exchangeable<int const> && !Exchangeable<int volatile> && !Exchangeable<int[2]>);
static_assert(!Exchangeable<int&> && !Exchangeable<Anchored> && !Exchangeable<ThrowingMove> && !Exchangeable<void>);

// swap is an object, so argument-dependent lookup never finds it, and each
// call is noexcept.
static_assert(std::is_same_v<decltype(swap), SwapValues const> && !std::is_function_v<decltype(swap)>);
static_assert(noexcept(swap(std::declval<int&>(), std::declval<int&>())));
static_assert(std::is_same_v<decltype(exchange(std::declval<int&>(), 1)), int>);

[[nodiscard]] consteval bool exchanges_a_value() {
    std::uint32_t slot = 3;
    std::uint32_t const old = exchange(slot, 7);
    return old == 3 && slot == 7;
}
static_assert(exchanges_a_value());

[[nodiscard]] consteval bool exchanges_a_value_that_only_moves() {
    OnlyMoves slot{3};
    OnlyMoves const old = exchange(slot, OnlyMoves{7});
    return old.value() == 3 && slot.value() == 7;
}
static_assert(exchanges_a_value_that_only_moves());

[[nodiscard]] consteval bool swaps_two_values() {
    OnlyMoves left{1};
    OnlyMoves right{2};
    swap(left, right);
    int first = 5;
    int second = 6;
    swap(first, second);
    // A swap of an object with itself keeps its value.
    swap(first, first);
    return left.value() == 2 && right.value() == 1 && first == 6 && second == 5;
}
static_assert(swaps_two_values());

// as_const gives a const reference to the same object, from a const object
// too.  The fixture neg_core_values_as_const_temporary holds that it refuses
// a temporary.
static_assert(std::is_same_v<decltype(as_const(std::declval<int&>())), int const&>);
static_assert(std::is_same_v<decltype(as_const(std::declval<int const&>())), int const&>);
static_assert(std::is_same_v<decltype(as_const(std::declval<Anchored&>())), Anchored const&>);
static_assert(noexcept(as_const(std::declval<OnlyMoves&>())));

[[nodiscard]] consteval bool views_the_same_object() {
    int count = 4;
    int const& view = as_const(count);
    count = 9;
    return &view == &count && view == 9;
}
static_assert(views_the_same_object());

}  // namespace foundation::core::detail::values_checks
