// Tests of foundation/core/Values.h at run time: exchange gives the old
// value and puts the new one, and swap exchanges two values, for words, for
// a class of two members and for a class that only moves.  A count of the
// moves shows that each operation moves each value and copies none.

#include <foundation/core/Values.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

namespace core = ::foundation::core;

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

struct Pair {
    std::uint64_t first = 0;
    std::uint32_t second = 0;
};

// A class that only moves.  Each move adds one to the count of moves of the
// value that it takes, and the source keeps no value.
class Tracked {
    std::uint64_t value_ = 0;
    int moves_ = 0;
    bool is_held_ = true;

public:
    explicit Tracked(std::uint64_t value) noexcept : value_{value} {}
    Tracked(Tracked&& other) noexcept : value_{other.value_}, moves_{other.moves_ + 1}, is_held_{other.is_held_} {
        other.is_held_ = false;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        value_ = other.value_;
        moves_ = other.moves_ + 1;
        is_held_ = other.is_held_;
        other.is_held_ = false;
        return *this;
    }
    Tracked(Tracked const&) = delete("a Tracked value only moves");
    Tracked& operator=(Tracked const&) = delete("a Tracked value only moves");

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] int moves() const noexcept { return moves_; }
    [[nodiscard]] bool is_held() const noexcept { return is_held_; }
};

void test_exchange_and_swap_of_words() {
    ::foundation::test::PhiloxStream stream{70};
    for (int index = 0; index < 100000; ++index) {
        std::uint64_t const first = stream.next_wide();
        std::uint64_t const second = stream.next_wide();
        std::uint64_t slot = opaque(first);
        CRUCIBLE_FATAL_INVARIANT(core::exchange(slot, opaque(second)) == first);
        CRUCIBLE_FATAL_INVARIANT(slot == second);
        std::uint64_t left = opaque(first);
        std::uint64_t right = opaque(second);
        core::swap(left, right);
        CRUCIBLE_FATAL_INVARIANT(left == second && right == first);
        core::swap(left, left);
        CRUCIBLE_FATAL_INVARIANT(left == second);
    }
}

void test_exchange_and_swap_of_a_class() {
    Pair slot{opaque(std::uint64_t{1}), opaque(std::uint32_t{2})};
    Pair const old = core::exchange(slot, Pair{3, 4});
    CRUCIBLE_FATAL_INVARIANT(old.first == 1 && old.second == 2 && slot.first == 3 && slot.second == 4);
    Pair other{5, 6};
    core::swap(slot, other);
    CRUCIBLE_FATAL_INVARIANT(slot.first == 5 && slot.second == 6 && other.first == 3 && other.second == 4);
}

// exchange moves the old value out and the new value in.  swap moves each
// value two times: into the held value or across, and back.
void test_each_value_moves_and_none_copies() {
    Tracked slot{opaque(std::uint64_t{11})};
    Tracked const old = core::exchange(slot, Tracked{22});
    CRUCIBLE_FATAL_INVARIANT(old.value() == 11 && old.is_held() && old.moves() >= 1);
    CRUCIBLE_FATAL_INVARIANT(slot.value() == 22 && slot.is_held());
    Tracked left{opaque(std::uint64_t{33})};
    Tracked right{opaque(std::uint64_t{44})};
    core::swap(left, right);
    CRUCIBLE_FATAL_INVARIANT(left.value() == 44 && right.value() == 33);
    CRUCIBLE_FATAL_INVARIANT(left.is_held() && right.is_held());
    CRUCIBLE_FATAL_INVARIANT(left.moves() == 1 && right.moves() == 2);
}

}  // namespace

int main() {
    test_exchange_and_swap_of_words();
    test_exchange_and_swap_of_a_class();
    test_each_value_moves_and_none_copies();
    return 0;
}
