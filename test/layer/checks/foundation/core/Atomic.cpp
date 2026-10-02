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

// The size of the value, and its alignment.
static_assert(sizeof(Atomic<std::uint64_t>) == 8 && alignof(Atomic<std::uint64_t>) == 8);
static_assert(sizeof(Atomic<std::uint32_t>) == 4 && alignof(Atomic<std::uint32_t>) == 4);
static_assert(sizeof(Atomic<std::uint8_t>) == 1);
static_assert(sizeof(Atomic<bool>) == 1);
static_assert(sizeof(Atomic<Phase>) == 1);
static_assert(sizeof(Atomic<WidePhase>) == 8 && alignof(Atomic<WidePhase>) == 8);
static_assert(sizeof(Tally) == 8 && alignof(Tally) == 8);
static_assert(sizeof(CasOutcome<std::uint64_t>) == 16);

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
static_assert(!AtomicValue<int const>);
static_assert(!AtomicValue<int volatile>);
static_assert(!AtomicValue<Wrapped>);
static_assert(!AtomicValue<float>);
static_assert(!AtomicValue<int*>);

// An addition steps an integer, and not a bool or an enum.
template <class A>
concept Adds = requires(A& cell) { cell.fetch_add_acq_rel(typename A::value_type{}); };
static_assert(Adds<Atomic<int>> && Adds<Atomic<std::uint64_t>>);
static_assert(!Adds<Atomic<bool>>);
static_assert(!Adds<Atomic<Phase>>);

// A borrow of a temporary CacheLine is refused.
template <class C>
concept BorrowsFromTemporary = requires(C&& line) { static_cast<C&&>(line).get(); };
static_assert(!BorrowsFromTemporary<CacheLine<int>>);

}  // namespace detail::atomic_checks

}  // namespace foundation::core
