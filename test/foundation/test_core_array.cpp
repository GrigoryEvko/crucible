// Tests of foundation/core/Index.h and foundation/core/Array.h: the checked
// index, the loop of the indexes, and the FixedArray with its View, its
// proven reads and its compare.

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>

#include "../test_assert.h"
#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

#include <array>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::IndexRange;
using ::foundation::core::View;

struct NoDefault {
    int value;
    explicit NoDefault(int from) noexcept : value{from} {}
    [[nodiscard]] friend bool operator==(NoDefault, NoDefault) noexcept = default;
};

// The check of an index is a value exactly when the number is below the
// count.  A plain compare is the model.
void test_index_check_agrees_with_the_model() {
    ::foundation::test::PhiloxStream stream{0x1D3C4ECC0000A001u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::size_t const raw = stream.below(40);
        auto checked = Index<32>::check(raw);
        assert(checked.is_some() == (raw < 32));
        for (Index<32> const position : checked) {
            assert(position.raw() == raw);
        }
    }
    assert(Index<1>::check(0).is_some());
    assert(Index<1>::check(1).is_none());
}

void test_index_range_visits_each_index_in_order() {
    std::size_t expected = 0;
    for (Index<6> const position : IndexRange<6>{}) {
        assert(position.raw() == expected);
        ++expected;
    }
    assert(expected == 6);
}

// A cursor of an IndexRange never makes an index past its range: a read at
// the end and a step past the end end the process in each build.
void test_index_cursor_at_the_end_aborts() {
    bool const end_read_aborts = ::foundation::test::aborts([] { static_cast<void>((*IndexRange<3>::end()).raw()); });
    bool const past_end_step_aborts = ::foundation::test::aborts([] {
        auto cursor = IndexRange<1>::begin();
        ++cursor;
        ++cursor;
    });
    assert(end_read_aborts && past_end_step_aborts);
}

void test_fixed_array_starts_with_zeros() {
    FixedArray<std::uint64_t, 16> const words;
    for (std::uint64_t const word : words) {
        assert(word == 0);
    }
    assert(words.size() == 16);
}

void test_fixed_array_reads_at_proven_indexes() {
    FixedArray<int, 4> numbers{4, 8, 15, 16};
    assert(numbers.at(index<0>()) == 4);
    assert(numbers.at(index<3>()) == 16);
    numbers.at(index<1>()) = 9;
    int total = 0;
    for (Index<4> const position : numbers.indices()) {
        total += numbers.at(position);
    }
    assert(total == 4 + 9 + 15 + 16);
    // An index from data goes through its check, and a number past the end
    // gives no element.
    std::size_t const from_data = 2;
    for (Index<4> const position : Index<4>::check(from_data)) {
        assert(numbers.at(position) == 15);
    }
    assert(Index<4>::check(from_data + 2).is_none());
    // An index of a smaller count widens.
    Index<2> const small = index<1>();
    assert(numbers.at(small) == 9);
}

void test_fixed_array_view_fill_and_copy() {
    FixedArray<std::uint32_t, 8> target;
    FixedArray<std::uint32_t, 8> source{1, 2, 3, 4, 5, 6, 7, 8};
    ::foundation::core::fill(target.view(), std::uint32_t{0xAB});
    for (std::uint32_t const value : target) {
        assert(value == 0xAB);
    }
    ::foundation::core::copy(target.view(), source.view());
    assert(target == source);
    View<std::uint32_t, 8> const elements = target.view();
    assert(elements.at(index<7>()) == 8);
    std::uint32_t sum = 0;
    for (Index<8> const position : elements.indices()) {
        sum += elements.at(position);
    }
    assert(sum == 36);
    // A window of the View writes the elements of the array.
    ::foundation::core::fill(target.view().window(2, 3).expect("three elements fit"), std::uint32_t{0});
    assert(target.at(index<1>()) == 2 && target.at(index<2>()) == 0 && target.at(index<4>()) == 0);
    assert(target.at(index<5>()) == 6);
    assert(!(target == source));
}

// get gives a Ref of the element below the count, and no value at or past
// the count, for a View of each extent and for a FixedArray.
void test_get_reads_below_the_count() {
    FixedArray<int, 4> numbers{4, 8, 15, 16};
    FixedArray<int, 4> const& fixed = numbers;
    View<int> const elements = numbers.view();
    assert(*elements.get(2).expect("the position is below the count") == 15);
    assert(elements.get(4).is_none());
    assert(elements.get(~std::size_t{0}).is_none());
    *elements.get(0).expect("the position is below the count") = 5;
    assert(numbers.at(index<0>()) == 5);

    View<int const, 4> const fixed_view = fixed.view();
    assert(*fixed_view.get(3).expect("the position is below the count") == 16);
    assert(fixed_view.get(4).is_none());

    *numbers.get(1).expect("the position is below the count") = 9;
    assert(numbers.at(index<1>()) == 9);
    assert(*fixed.get(1).expect("the position is below the count") == 9);
    assert(fixed.get(4).is_none());

    // A Ref from get reaches the element of the array.  Each call of get has
    // a fresh brand, and same_object accepts two brands.
    auto const third = numbers.get(2).expect("the position is below the count");
    assert(::foundation::core::same_object(third, numbers.get(2).expect("the position is below the count")));

    View<int const> const empty{};
    assert(empty.get(0).is_none());
}

void test_fixed_array_of_a_type_with_no_default() {
    FixedArray<NoDefault, 3> const values{NoDefault{1}, NoDefault{2}, NoDefault{3}};
    int total = 0;
    for (NoDefault const& value : values) {
        total += value.value;
    }
    assert(total == 6);
    FixedArray<NoDefault, 3> const same{NoDefault{1}, NoDefault{2}, NoDefault{3}};
    assert(values == same);
}

// The property: random writes at checked indexes give the elements of the
// same writes into a std::array.
void test_fixed_array_agrees_with_std_array() {
    ::foundation::test::PhiloxStream stream{0xA77A1E5E0000B002u};
    FixedArray<std::uint64_t, 64> array;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::array is the model of the writes of a FixedArray")
    std::array<std::uint64_t, 64> model{};
    for (int trial = 0; trial < 8192; ++trial) {
        std::size_t const raw = stream.below(80);
        std::uint64_t const value = stream.next_wide();
        for (Index<64> const position : Index<64>::check(raw)) {
            array.at(position) = value;
        }
        if (raw < model.size()) model[raw] = value;
    }
    std::size_t position = 0;
    for (std::uint64_t const value : array) {
        assert(value == model[position]);
        ++position;
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

}  // namespace

int main() {
    test_index_check_agrees_with_the_model();
    test_index_range_visits_each_index_in_order();
    test_index_cursor_at_the_end_aborts();
    test_fixed_array_starts_with_zeros();
    test_fixed_array_reads_at_proven_indexes();
    test_fixed_array_view_fill_and_copy();
    test_get_reads_below_the_count();
    test_fixed_array_of_a_type_with_no_default();
    test_fixed_array_agrees_with_std_array();
    crucible::test::pass("test_core_array: all tests passed\n");
    return 0;
}
