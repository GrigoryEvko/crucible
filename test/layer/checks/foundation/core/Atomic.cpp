// The compile-time checks of foundation/core/Atomic.h.

#include <foundation/core/Atomic.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::atomic_checks {

enum class Phase : std::uint8_t {
    idle,
    busy
};
enum class WidePhase : std::uint64_t {
    idle,
    busy
};

struct Wrapped {
    int value = 0;
};

// Two words with no padding: one bit pattern for each value.
struct Pair {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

// Six bytes of members and two bytes of padding.
struct Padded {
    std::uint32_t key = 0;
    std::uint16_t tag = 0;
};

struct NotDefaultConstructible {
    explicit constexpr NotDefaultConstructible(std::uint32_t initial) noexcept : value{initial} {}
    std::uint32_t value;
};

// The size of the value, and its alignment.
static_assert(sizeof(Atomic<std::uint64_t>) == 8 && alignof(Atomic<std::uint64_t>) == 8);
static_assert(sizeof(Atomic<std::uint32_t>) == 4 && alignof(Atomic<std::uint32_t>) == 4);
static_assert(sizeof(Atomic<std::uint8_t>) == 1);
static_assert(sizeof(Atomic<bool>) == 1);
static_assert(sizeof(Atomic<Phase>) == 1);
static_assert(sizeof(Atomic<WidePhase>) == 8 && alignof(Atomic<WidePhase>) == 8);
static_assert(sizeof(Atomic<Pair>) == 8 && alignof(Atomic<Pair>) == 8);
static_assert(sizeof(Atomic<Wrapped>) == 4 && alignof(Atomic<Wrapped>) == 4);
static_assert(sizeof(Tally) == 8 && alignof(Tally) == 8);
// The result of a compare-and-swap is the read value and one flag, so the
// ABI gives it back in two registers.
static_assert(sizeof(Result<Unit, CasRefusal<std::uint64_t>>) == 16);
static_assert(std::is_trivially_copyable_v<Result<Unit, CasRefusal<std::uint64_t>>>);

// One cache line for a small cell, and whole lines for a larger one.
static_assert(sizeof(CacheLine<Atomic<std::uint64_t>>) == 64 && alignof(CacheLine<Atomic<std::uint64_t>>) == 64);
static_assert(sizeof(CacheLine<Tally>) == 64);
static_assert(sizeof(CacheLine<std::uint8_t>) == 64);
struct NinetySixBytes {
    std::uint64_t word_0 = 0, word_1 = 0, word_2 = 0, word_3 = 0, word_4 = 0, word_5 = 0;
    std::uint64_t word_6 = 0, word_7 = 0, word_8 = 0, word_9 = 0, word_10 = 0, word_11 = 0;
};
static_assert(sizeof(NinetySixBytes) == 96 && sizeof(CacheLine<NinetySixBytes>) == 128);

// The address of a cell is its identity, so a cell does not move.
static_assert(!std::is_copy_constructible_v<Atomic<int>> && !std::is_move_constructible_v<Atomic<int>>);
static_assert(!std::is_copy_assignable_v<Atomic<int>> && !std::is_move_assignable_v<Atomic<int>>);
static_assert(!std::is_copy_constructible_v<Tally> && !std::is_move_constructible_v<Tally>);
static_assert(!std::is_copy_constructible_v<CacheLine<Atomic<int>>>);

// No operator and no conversion: each operation names its order.
static_assert(!std::is_convertible_v<Atomic<int>&, int>);
static_assert(!std::is_assignable_v<Atomic<int>&, int>);
template <class A>
concept Increments = requires(A& cell) { ++cell; };
static_assert(!Increments<Atomic<int>>);

// The value gate.
static_assert(AtomicValue<int> && AtomicValue<std::uint64_t> && AtomicValue<bool> && AtomicValue<Phase>);
static_assert(AtomicValue<Wrapped> && AtomicValue<Pair> && AtomicValue<int*>);
static_assert(AtomicValue<NotDefaultConstructible>);
static_assert(!AtomicValue<int const>);
static_assert(!AtomicValue<int volatile>);
static_assert(!AtomicValue<Padded>);
static_assert(!AtomicValue<float>);
static_assert(!AtomicValue<double>);
static_assert(!AtomicValue<int[2]>);
static_assert(!AtomicValue<int&>);

// A value with no default constructor gives a cell with no default
// constructor, and the cell never holds bits that no constructor made.
static_assert(!std::is_default_constructible_v<Atomic<NotDefaultConstructible>>);
static_assert(std::is_default_constructible_v<Atomic<Pair>>);

// A cell of a class value holds the bits of the value, and the constructor
// works in a constant evaluation, so a constinit cell needs no dynamic
// initialization.
[[nodiscard]] consteval bool builds_in_a_constant_evaluation() noexcept {
    Atomic<Pair> const pair_cell{Pair{3, 4}};
    Atomic<Pair> const default_cell{};
    Atomic<int*> const pointer_cell{nullptr};
    static_cast<void>(pair_cell);
    static_cast<void>(default_cell);
    static_cast<void>(pointer_cell);
    return true;
}
static_assert(builds_in_a_constant_evaluation());

// The arithmetic steps an integer, and not a bool or an enum.  A bitwise
// operation takes an unsigned integer.
template <class A>
concept Adds = requires(A& cell) { cell.fetch_add_acq_rel(typename A::value_type{}); };
template <class A>
concept Subtracts = requires(A& cell) { cell.fetch_sub_acq_rel(typename A::value_type{}); };
template <class A>
concept Bounds = requires(A& cell) {
    cell.fetch_max_acq_rel(typename A::value_type{});
    cell.fetch_min_acq_rel(typename A::value_type{});
};
template <class A>
concept SetsBits = requires(A& cell) {
    cell.fetch_or_acq_rel(typename A::value_type{});
    cell.fetch_and_acq_rel(typename A::value_type{});
};
template <class A>
concept Exchanges = requires(A& cell) { cell.exchange_acq_rel(typename A::value_type{}); };
static_assert(Adds<Atomic<int>> && Adds<Atomic<std::uint64_t>>);
static_assert(!Adds<Atomic<bool>> && !Adds<Atomic<Phase>> && !Adds<Atomic<Pair>>);
static_assert(Subtracts<Atomic<int>> && !Subtracts<Atomic<bool>> && !Subtracts<Atomic<Phase>>);
static_assert(Bounds<Atomic<std::int64_t>> && Bounds<Atomic<std::uint8_t>>);
static_assert(!Bounds<Atomic<bool>> && !Bounds<Atomic<Phase>> && !Bounds<Atomic<Pair>>);
static_assert(SetsBits<Atomic<std::uint32_t>> && SetsBits<Atomic<std::uint8_t>>);
static_assert(!SetsBits<Atomic<int>> && !SetsBits<Atomic<bool>> && !SetsBits<Atomic<Phase>>);
static_assert(Exchanges<Atomic<int>> && Exchanges<Atomic<bool>> && Exchanges<Atomic<Phase>> && Exchanges<Atomic<Pair>>);

// A borrow of a temporary CacheLine is refused.
template <class C>
concept BorrowsFromTemporary = requires(C&& line) { static_cast<C&&>(line).get(); };
static_assert(!BorrowsFromTemporary<CacheLine<int>>);

}  // namespace detail::atomic_checks

}  // namespace foundation::core
