// Tests of foundation/core/Ref.h: Ref, same_object and Option<Ref<T>>.

#include <foundation/core/Ref.h>

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

using ::foundation::core::none;
using ::foundation::core::Option;
using ::foundation::core::Ref;
using ::foundation::core::same_object;

struct Pair {
    int first = 0;
    int second = 0;
};

// A class whose operator& gives no address.  to() takes the address of the
// object and not the result of the operator.
struct HidesAddress {
    int value = 0;
    int operator&() const noexcept { return 0; }
};

struct Shape {
    virtual ~Shape() = default;
    [[nodiscard]] virtual int corners() const noexcept = 0;
};

struct Square final : Shape {
    [[nodiscard]] int corners() const noexcept override { return 4; }
};

void test_reach_the_object() {
    int number = 41;
    Ref<int> const ref = Ref<int>::to(number);
    *ref += 1;
    CRUCIBLE_FATAL_INVARIANT(number == 42);
    CRUCIBLE_FATAL_INVARIANT(*ref == 42);

    Pair pair{3, 4};
    Ref<Pair> const pair_ref = Ref<Pair>::to(pair);
    pair_ref->second = 5;
    CRUCIBLE_FATAL_INVARIANT(pair.second == 5 && (*pair_ref).first == 3);
}

void test_const_conversion() {
    int number = 7;
    Ref<int> const ref = Ref<int>::to(number);
    Ref<int const> const fixed = ref;
    CRUCIBLE_FATAL_INVARIANT(*fixed == 7);
    number = 8;
    CRUCIBLE_FATAL_INVARIANT(*fixed == 8);
    CRUCIBLE_FATAL_INVARIANT(same_object(ref, fixed));
}

void test_same_object() {
    int first = 1;
    int second = 1;
    Ref<int> const first_ref = Ref<int>::to(first);
    Ref<int> const first_again = Ref<int>::to(first);
    Ref<int> const second_ref = Ref<int>::to(second);
    CRUCIBLE_FATAL_INVARIANT(same_object(first_ref, first_again));
    CRUCIBLE_FATAL_INVARIANT(!same_object(first_ref, second_ref));

    HidesAddress hidden{9};
    Ref<HidesAddress> const hidden_ref = Ref<HidesAddress>::to(hidden);
    CRUCIBLE_FATAL_INVARIANT(hidden_ref->value == 9);
    CRUCIBLE_FATAL_INVARIANT(same_object(hidden_ref, Ref<HidesAddress>::to(hidden)));
}

// A copy reaches the same object, and an assignment makes the target reach
// the object of the source.
void test_copy_and_rebind() {
    int first = 1;
    int second = 2;
    Ref<int> ref = Ref<int>::to(first);
    Ref<int> const copy = ref;
    CRUCIBLE_FATAL_INVARIANT(same_object(ref, copy));
    ref = Ref<int>::to(second);
    CRUCIBLE_FATAL_INVARIANT(*ref == 2 && *copy == 1);
    CRUCIBLE_FATAL_INVARIANT(first == 1 && second == 2);
}

// A Ref of an abstract class reaches the object of the derived class.
void test_abstract_target() {
    Square square{};
    Ref<Shape const> const shape = Ref<Shape const>::to(square);
    CRUCIBLE_FATAL_INVARIANT(shape->corners() == 4);
}

void test_option_of_ref() {
    int number = 5;
    Option<Ref<int>> empty{none};
    CRUCIBLE_FATAL_INVARIANT(!empty.is_some());
    Option<Ref<int>> held = Option<Ref<int>>::some(Ref<int>::to(number));
    CRUCIBLE_FATAL_INVARIANT(held.is_some());
    int turns = 0;
    for (Ref<int> const& ref : held) {
        *ref = 6;
        ++turns;
    }
    CRUCIBLE_FATAL_INVARIANT(turns == 1 && number == 6);
    Ref<int> const taken = static_cast<Option<Ref<int>>&&>(held).expect("the Option holds a Ref");
    CRUCIBLE_FATAL_INVARIANT(same_object(taken, Ref<int>::to(number)));
}

// Four slots, which a plain index selects.
struct Slots {
    std::uint64_t zero = 0;
    std::uint64_t one = 3;
    std::uint64_t two = 6;
    std::uint64_t three = 9;
};

[[nodiscard]] Ref<std::uint64_t> slot_at(Slots& slots, std::uint64_t index) noexcept {
    switch (index) {
        case 0:
            return Ref<std::uint64_t>::to(slots.zero);
        case 1:
            return Ref<std::uint64_t>::to(slots.one);
        case 2:
            return Ref<std::uint64_t>::to(slots.two);
        default:
            return Ref<std::uint64_t>::to(slots.three);
    }
}

// The property: Refs to random slots reach the slot that a plain index
// selects, and same_object agrees with the equality of the indices.
void test_agrees_with_plain_model() {
    Slots slots{};
    ::foundation::test::PhiloxStream stream{0x5EEDB0C5000000C3u};
    for (int trial = 0; trial < 512; ++trial) {
        std::uint64_t const first_index = stream.below(4);
        std::uint64_t const second_index = stream.below(4);
        Ref<std::uint64_t> const first = slot_at(slots, first_index);
        Ref<std::uint64_t const> const second = slot_at(slots, second_index);
        CRUCIBLE_FATAL_INVARIANT(*first == first_index * 3);
        CRUCIBLE_FATAL_INVARIANT(same_object(first, second) == (first_index == second_index));
    }
}

}  // namespace

int main() {
    test_reach_the_object();
    test_const_conversion();
    test_same_object();
    test_copy_and_rebind();
    test_abstract_target();
    test_option_of_ref();
    test_agrees_with_plain_model();
    return 0;
}
