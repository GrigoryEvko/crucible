// The compile-time checks of foundation/core/EnumMatch.h.

#include <foundation/core/EnumMatch.h>

#include <foundation/ByteSeal.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::enum_match_checks {

enum class Strength : std::uint8_t {
    none,
    acquire,
    release,
    seq_cst
};
enum class Order : std::uint8_t {
    none,
    acquire,
    release,
    full
};
// Two enumerators with one value, and values over more than 64 numbers.
enum class Sparse : std::int16_t {
    low = -300,
    middle = 7,
    alias = 7,
    high = 9000
};
// An open enumeration: each value of its underlying type is a value.
enum class[[= open_values{}]] Code : std::uint16_t {
    one = 1,
    two = 2
};

inline constexpr auto kOrdering = enumerators<Strength::acquire, Strength::release>;
inline constexpr auto kEnds = enumerators<Strength::none, Strength::seq_cst>;

// A set holds its enumerators in order.  The sets of Strength are disjoint,
// and a set that shares an enumerator with one of them is not.
static_assert(kOrdering.count == 2 && kOrdering.values[0] == Strength::acquire);
static_assert(std::is_same_v<decltype(kOrdering)::Enum, Strength>);
static_assert(are_disjoint<kOrdering, kEnds>());
static_assert(!are_disjoint<kOrdering, enumerators<Strength::release>>());

// A set names each enumerator one time: the fixture
// neg_core_enumerators_twice holds the refusal of a second name.
static_assert(enumerators<Strength::none, Strength::release>.values[1] == Strength::release);

// One arm for each enumerator.
[[nodiscard]] consteval Order order_of(Strength strength) {
    return match_enum(strength, arm<Strength::none>([] noexcept { return Order::none; }),
                      arm<Strength::acquire>([] noexcept { return Order::acquire; }),
                      arm<Strength::release>([] noexcept { return Order::release; }),
                      arm<Strength::seq_cst>([] noexcept { return Order::full; }));
}
static_assert(order_of(Strength::none) == Order::none && order_of(Strength::acquire) == Order::acquire);
static_assert(order_of(Strength::release) == Order::release && order_of(Strength::seq_cst) == Order::full);

// Stated sets, and a set beside an enumerator in one arm.
[[nodiscard]] consteval int rank_of(Strength strength) {
    return match_enum(strength, arm<kOrdering>([] noexcept { return 1; }), arm<kEnds>([] noexcept { return 2; }));
}
static_assert(rank_of(Strength::acquire) == 1 && rank_of(Strength::release) == 1);
static_assert(rank_of(Strength::none) == 2 && rank_of(Strength::seq_cst) == 2);

[[nodiscard]] consteval int mixed_of(Strength strength) {
    return match_enum(strength, arm<kOrdering, Strength::none>([] noexcept { return 1; }),
                      arm<Strength::seq_cst>([] noexcept { return 3; }));
}
static_assert(mixed_of(Strength::none) == 1 && mixed_of(Strength::seq_cst) == 3);

// Two enumerators with one value are one value: an arm names it with either
// name.  The values lie over more than 64 numbers.
[[nodiscard]] consteval int sparse_of(Sparse sparse) {
    return match_enum(sparse, arm<Sparse::low>([] noexcept { return 1; }),
                      arm<Sparse::alias>([] noexcept { return 2; }), arm<Sparse::high>([] noexcept { return 3; }));
}
static_assert(sparse_of(Sparse::low) == 1 && sparse_of(Sparse::middle) == 2 && sparse_of(Sparse::high) == 3);

// The arm otherwise of an open enumeration gets each value that no other
// arm names, also a value that names no enumerator.
[[nodiscard]] consteval std::uint16_t code_of(Code code) {
    return match_enum(code, arm<Code::one>([] noexcept { return std::uint16_t{100}; }),
                      otherwise([](Code other) noexcept { return std::to_underlying(other); }));
}
static_assert(code_of(Code::one) == 100 && code_of(Code::two) == 2 && code_of(enum_from<Code>(4000)) == 4000);

// The result type is the result of the arms, and a match of arms with no
// result gives void.
static_assert(
    std::is_same_v<decltype(match_enum(Strength::none, arm<kOrdering, kEnds>([] noexcept { return 1L; }))), long>);
static_assert(std::is_same_v<decltype(match_enum(Strength::none, arm<kOrdering, kEnds>([] noexcept {}))), void>);

}  // namespace detail::enum_match_checks

}  // namespace foundation::core
