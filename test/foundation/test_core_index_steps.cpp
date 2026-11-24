// Tests of the cyclic steps of an Index and of Ranged of
// foundation/core/Index.h at run time.  Each value comes through a call that
// the optimizer cannot read, so each step, each check and each operation
// runs at run time and not in a constant evaluation.  next_cyclic and
// prev_cyclic walk three turns of rings of power-of-two counts and of other
// counts.  check of a Ranged agrees with an exact compare for each integer
// type, and each operation agrees with the operation of std::int64_t.

#include <foundation/core/Index.h>

#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace core = ::foundation::core;

// The value, through a call that the optimizer cannot read.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// Three turns of a ring of Count slots, forward, with the step back of each
// position.
template <std::size_t Count>
void check_ring_walk() {
    auto forward = core::Index<Count>::check(opaque(std::size_t{0})).expect("0 is below the count");
    for (std::size_t step = 1; step <= 3 * Count; ++step) {
        forward = forward.next_cyclic();
        CRUCIBLE_FATAL_INVARIANT(forward.raw() == step % Count);
        CRUCIBLE_FATAL_INVARIANT(forward.prev_cyclic().raw() == (step + Count - 1) % Count);
    }
}

void test_ring_walks() {
    check_ring_walk<1>();
    check_ring_walk<2>();
    check_ring_walk<5>();
    check_ring_walk<8>();
    check_ring_walk<1000>();
    check_ring_walk<1024>();
}

// check of a range that holds negative values, for a signed and an unsigned
// type of each width, over values around the range.
void test_ranged_check_of_each_type() {
    using Small = core::Ranged<-3, 3>;
    for (int value = -10; value <= 10; ++value) {
        bool const is_inside = value >= -3 && value <= 3;
        bool const is_inside_unsigned = value >= 0 && value <= 3;
        CRUCIBLE_FATAL_INVARIANT(Small::check(opaque(value)).is_some() == is_inside);
        CRUCIBLE_FATAL_INVARIANT(Small::check(opaque(static_cast<std::int8_t>(value))).is_some() == is_inside);
        CRUCIBLE_FATAL_INVARIANT(Small::check(opaque(static_cast<std::int64_t>(value))).is_some() == is_inside);
        CRUCIBLE_FATAL_INVARIANT(Small::check(opaque(static_cast<std::uint8_t>(value))).is_some()
                                 == is_inside_unsigned);
        CRUCIBLE_FATAL_INVARIANT(Small::check(opaque(static_cast<std::uint64_t>(value))).is_some()
                                 == is_inside_unsigned);
    }
    using Wide = core::Ranged<0, core::max_of<std::int64_t>>;
    CRUCIBLE_FATAL_INVARIANT(Wide::check(opaque(core::max_of<std::uint64_t>)).is_none());
    CRUCIBLE_FATAL_INVARIANT(Wide::check(opaque(static_cast<std::uint64_t>(core::max_of<std::int64_t>))).is_some());
    CRUCIBLE_FATAL_INVARIANT(Wide::check(opaque(core::min_of<std::int64_t>)).is_none());
}

// Each sum, difference and product of a value of [-4, 4] and a value of
// [0, 7] is the operation of std::int64_t, in the range of its type.
void test_ranged_arithmetic() {
    using Left = core::Ranged<-4, 4>;
    using Right = core::Ranged<0, 7>;
    for (std::int64_t left = -4; left <= 4; ++left) {
        for (std::int64_t right = 0; right <= 7; ++right) {
            Left const ranged_left = Left::check(opaque(left)).expect("the value is in the range");
            Right const ranged_right = Right::check(opaque(right)).expect("the value is in the range");
            CRUCIBLE_FATAL_INVARIANT((ranged_left + ranged_right).raw() == left + right);
            CRUCIBLE_FATAL_INVARIANT((ranged_left - ranged_right).raw() == left - right);
            CRUCIBLE_FATAL_INVARIANT((ranged_right - ranged_left).raw() == right - left);
            CRUCIBLE_FATAL_INVARIANT((ranged_left * ranged_right).raw() == left * right);
        }
    }
}

// The conversions to an Index and from an Index keep the number, and narrow
// refuses a value outside the smaller range.
void test_ranged_conversions() {
    for (std::size_t position = 0; position < 8; ++position) {
        auto const ranged = core::Ranged<0, 7>::check(opaque(position)).expect("the position is below 8");
        core::Index<8> const slot = ranged;
        CRUCIBLE_FATAL_INVARIANT(slot.raw() == position);
        core::Ranged<-1, 9> const wider = slot;
        CRUCIBLE_FATAL_INVARIANT(static_cast<std::size_t>(wider.raw()) == position);
        bool const is_in_smaller_range = ranged.narrow<0, 3>().is_some();
        CRUCIBLE_FATAL_INVARIANT(is_in_smaller_range == (position <= 3));
    }
}

}  // namespace

int main() {
    test_ring_walks();
    test_ranged_check_of_each_type();
    test_ranged_arithmetic();
    test_ranged_conversions();
    return 0;
}
