// Tests of foundation/core/Index.h and foundation/core/Array.h: the checked
// index, the loop of the indexes, and the FixedArray with its View, its
// proven reads and its compare.

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/core/List.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

#include <array>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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

// A linear end, for the bodies of the element door and the frame door.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LinearEnd {
    int handle = 0;
    constexpr explicit LinearEnd(int value) noexcept : handle{value} {}
    constexpr LinearEnd(LinearEnd&&) noexcept = default;
    constexpr LinearEnd& operator=(LinearEnd&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    LinearEnd(LinearEnd const&) = delete("a linear end of the test does not copy");
    LinearEnd& operator=(LinearEnd const&) = delete("a linear end of the test does not copy");
};

// The check of an index is a value exactly when the number is below the
// count.  A plain compare is the model.
void test_index_check_agrees_with_the_model() {
    ::crucible::test::PhiloxStream stream{0x1D3C4ECC0000A001u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::size_t const raw = stream.below(40);
        auto checked = Index<32>::check(raw);
        CRUCIBLE_TEST_ASSERT(kTest, checked.is_some() == (raw < 32));
        for (Index<32> const position : checked) {
            CRUCIBLE_TEST_ASSERT(kTest, position.raw() == raw);
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, Index<1>::check(0).is_some());
    CRUCIBLE_TEST_ASSERT(kTest, Index<1>::check(1).is_none());
}

void test_index_range_visits_each_index_in_order() {
    std::size_t expected = 0;
    for (Index<6> const position : IndexRange<6>{}) {
        CRUCIBLE_TEST_ASSERT(kTest, position.raw() == expected);
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected == 6);
}

// A cursor of an IndexRange never makes an index past its range: a read at
// the end and a step past the end end the process in each build.
// Each one runs in a child through expect_dies of the Check family.
void test_index_cursor_at_the_end_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test, [] noexcept { static_cast<void>((*IndexRange<3>::end()).raw()); },
        ::fixy::TextView{"a read through the end cursor of an IndexRange"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            auto cursor = IndexRange<1>::begin();
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"a step past the end cursor of an IndexRange"});
}

void test_fixed_array_starts_with_zeros() {
    FixedArray<std::uint64_t, 16> const words;
    for (std::uint64_t const word : words) {
        CRUCIBLE_TEST_ASSERT(kTest, word == 0);
    }
    CRUCIBLE_TEST_ASSERT(kTest, words.size() == 16);
}

// An element whose default constructor is not trivial: it starts with the
// value 7.
struct Seven {
    int value = 7;
};

// An array past the bound of the list constructor still value-initializes
// each element.  The array lives in static storage, because it holds 256 KB.
void test_large_fixed_array_value_initializes() {
    static FixedArray<Seven, 65536> sevens;
    static FixedArray<std::uint32_t, 65536> zeros;
    std::size_t sevens_seen = 0;
    for (Seven const& element : sevens) {
        if (element.value == 7) ++sevens_seen;
    }
    std::size_t zeros_seen = 0;
    for (std::uint32_t const element : zeros) {
        if (element == 0) ++zeros_seen;
    }
    CRUCIBLE_TEST_ASSERT(kTest, sevens_seen == 65536 && zeros_seen == 65536);
    sevens.with_element(index<65535>(), [](Seven& element) noexcept { element.value = 8; });
    CRUCIBLE_TEST_ASSERT(kTest, sevens.at(Index<65536>::check(65535).expect("65535 is below 65536")).value == 8);
}

void test_fixed_array_reads_at_proven_indexes() {
    FixedArray<int, 4> numbers{4, 8, 15, 16};
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(index<0>()) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(index<3>()) == 16);
    numbers.with_element(index<1>(), [](int& element) noexcept { element = 9; });
    int total = 0;
    for (Index<4> const position : numbers.indices()) {
        total += numbers.at(position);
    }
    CRUCIBLE_TEST_ASSERT(kTest, total == 4 + 9 + 15 + 16);
    // An index from data goes through its check, and a number past the end
    // gives no element.
    std::size_t const from_data = 2;
    for (Index<4> const position : Index<4>::check(from_data)) {
        CRUCIBLE_TEST_ASSERT(kTest, numbers.at(position) == 15);
    }
    CRUCIBLE_TEST_ASSERT(kTest, Index<4>::check(from_data + 2).is_none());
    // An index of a smaller count widens.
    Index<2> const small = index<1>();
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(small) == 9);
}

// The element door writes one element in place and gives the result of its
// body.  The door of a const array reads, and each other element keeps its
// value.
void test_fixed_array_element_door_writes_in_place() {
    FixedArray<std::uint64_t, 4> words{1, 2, 4, 8};
    words.with_element(index<2>(), [](std::uint64_t& word) noexcept { word |= 16; });
    int const steps = words.with_element(index<0>(), [](std::uint64_t& word) noexcept {
        ++word;
        return 1;
    });
    FixedArray<std::uint64_t, 4> const& read = words;
    std::uint64_t const third = read.with_element(index<2>(), [](std::uint64_t const& word) noexcept { return word; });
    CRUCIBLE_TEST_ASSERT(kTest, steps == 1 && third == 20);
    CRUCIBLE_TEST_ASSERT(kTest, (words == FixedArray<std::uint64_t, 4>{2, 2, 20, 8}));
}

// One element of the value door: a value and the count of the calls that
// built it.
struct Built {
    int value = 0;
    int calls = 0;
};

// The function of the value door for the test: it counts its calls through
// the reference argument.
[[nodiscard]] Built build_counted(int value, int& calls) noexcept {
    ++calls;
    return Built{.value = value, .calls = calls};
}

// The value door builds the element at its index from fn(args...), one call
// for each door, and each other element keeps its value.  A function and a
// lambda with no capture serve as fn, and a reference argument carries a
// count back to the caller.
void test_fixed_array_value_door_builds_in_place() {
    FixedArray<Built, 3> built{};
    int calls = 0;
    built.build_at(index<1>(), build_counted, 7, calls);
    built.build_at(
        Index<3>::check(2).expect("2 is below 3"),
        [](int value, int& count) noexcept { return Built{.value = value * 2, .calls = ++count}; }, 5, calls);
    CRUCIBLE_TEST_ASSERT(kTest, calls == 2);
    CRUCIBLE_TEST_ASSERT(kTest, built.at(index<0>()).value == 0 && built.at(index<0>()).calls == 0);
    CRUCIBLE_TEST_ASSERT(kTest, built.at(index<1>()).value == 7 && built.at(index<1>()).calls == 1);
    CRUCIBLE_TEST_ASSERT(kTest, built.at(index<2>()).value == 10 && built.at(index<2>()).calls == 2);
    built.build_at(index<1>(), build_counted, 3, calls);
    CRUCIBLE_TEST_ASSERT(kTest, built.at(index<1>()).value == 3 && built.at(index<1>()).calls == 3);

    // An array of one element, which GCC reads as an array of one byte at
    // the end of its class.
    FixedArray<Built, 1> one{};
    one.build_at(index<0>(), build_counted, 4, calls);
    CRUCIBLE_TEST_ASSERT(kTest, one.at(index<0>()).value == 4 && one.at(index<0>()).calls == 4);
}

// set writes one element, and exchange writes one element and gives back the
// value that it held.  Each other element keeps its value.  An element that
// only moves, such as a HeapArray, takes the two doors.
void test_fixed_array_value_doors_write_one_element() {
    FixedArray<int, 4> numbers{1, 2, 3, 4};
    numbers.set(index<1>(), 20);
    int const seven = 7;
    numbers.set(Index<4>::check(3).expect("3 is below 4"), seven);
    int const held = numbers.exchange(index<0>(), 10);
    CRUCIBLE_TEST_ASSERT(kTest, held == 1 && (numbers == FixedArray<int, 4>{10, 20, 3, 7}));
    numbers.set(index<2>(), numbers.at(index<0>()));
    CRUCIBLE_TEST_ASSERT(kTest, (numbers == FixedArray<int, 4>{10, 20, 10, 7}));

    auto const alloc = ::foundation::effects::testing::test().alloc;
    FixedArray<::foundation::core::HeapArray<int>, 2> runs{};
    runs.set(index<1>(), ::foundation::core::mint_heap_array(alloc, 3, 5));
    ::foundation::core::HeapArray<int> const taken =
        runs.exchange(index<1>(), ::foundation::core::mint_heap_array(alloc, 1, 6));
    CRUCIBLE_TEST_ASSERT(kTest,
                         taken.size() == 3 && runs.at(index<1>()).size() == 1 && runs.at(index<0>()).size() == 0);
}

// The value doors of the run: fill writes each element, copy_from the N
// elements of a View, and copy_window a part that fits.  A window that does
// not fit writes nothing.
void test_fixed_array_value_doors_write_the_run() {
    FixedArray<std::uint32_t, 8> target;
    FixedArray<std::uint32_t, 8> const source{1, 2, 3, 4, 5, 6, 7, 8};
    target.fill(0xAB);
    CRUCIBLE_TEST_ASSERT(
        kTest, target.view().walk().all_of([](std::uint32_t const& value) noexcept { return value == 0xAB; }));
    target.copy_from(source.view());
    CRUCIBLE_TEST_ASSERT(kTest, target == source);

    View<std::uint32_t const> const two = source.view().window(0, 2).expect("two elements fit");
    CRUCIBLE_TEST_ASSERT(kTest, target.copy_window(6, two).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, (target == FixedArray<std::uint32_t, 8>{1, 2, 3, 4, 5, 6, 1, 2}));
    CRUCIBLE_TEST_ASSERT(kTest, target.copy_window(7, two).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, target.copy_window(9, View<std::uint32_t const>{}).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, target.copy_window(8, View<std::uint32_t const>{}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, (target == FixedArray<std::uint32_t, 8>{1, 2, 3, 4, 5, 6, 1, 2}));
}

// A copy from a View of the same array gives the array the values that the
// View held before the copy, for each direction of the overlap.
void test_fixed_array_copy_doors_take_their_own_run() {
    FixedArray<std::uint32_t, 8> up{1, 2, 3, 4, 5, 6, 7, 8};
    CRUCIBLE_TEST_ASSERT(kTest, up.copy_window(2, up.view().window(0, 6).expect("six elements fit")).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, (up == FixedArray<std::uint32_t, 8>{1, 2, 1, 2, 3, 4, 5, 6}));
    FixedArray<std::uint32_t, 8> down{1, 2, 3, 4, 5, 6, 7, 8};
    CRUCIBLE_TEST_ASSERT(kTest, down.copy_window(0, down.view().window(2, 6).expect("six elements fit")).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, (down == FixedArray<std::uint32_t, 8>{3, 4, 5, 6, 7, 8, 7, 8}));
    down.copy_from(down.view());
    CRUCIBLE_TEST_ASSERT(kTest, (down == FixedArray<std::uint32_t, 8>{3, 4, 5, 6, 7, 8, 7, 8}));
}

// The build door gives the array that its body wrote, alone for a body that
// gives void, and after the result for each other body.  Each element that
// the body does not write keeps its value initialization.  A constant
// evaluation takes the door too.
void test_build_fixed_array_gives_a_new_array() {
    FixedArray<int, 4> const squares = ::foundation::core::build_fixed_array<int, 4>([](auto run) noexcept {
        int next = 0;
        for (int& element : run) {
            element = next * next;
            ++next;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, (squares == FixedArray<int, 4>{0, 1, 4, 9}));

    FixedArray<std::uint32_t, 2> const words{5, 6};
    auto [count, head] = ::foundation::core::build_fixed_array<std::uint32_t, 3>([&words](auto run) noexcept {
        ::foundation::core::copy(::foundation::core::take_window<2>(std::move(run), 0).expect("two elements fit"),
                                 words.view());
        return words.size();
    });
    CRUCIBLE_TEST_ASSERT(kTest, count == 2 && (head == FixedArray<std::uint32_t, 3>{5, 6, 0}));

    // A body that writes another array of the same type through a capture
    // by value of its View reads it, and the two frames have two brands.
    FixedArray<std::uint32_t, 3> const doubled =
        ::foundation::core::build_fixed_array<std::uint32_t, 3>([from = head.view()](auto run) noexcept {
            for (Index<3> const at : from.indices()) {
                auto element = run.get_mut(at.raw()).expect("an index of the View is an index of the run");
                *element = from.at(at) * 2;
            }
        });
    CRUCIBLE_TEST_ASSERT(kTest, (doubled == FixedArray<std::uint32_t, 3>{10, 12, 0}));
    static constexpr FixedArray<int, 2> threes = ::foundation::core::build_fixed_array<int, 2>([](auto run) noexcept {
        for (int& element : run)
            element = 3;
    });
    static_assert(threes.at(index<1>()) == 3);
}

// A body of the element door and of the frame door reads a linear local of
// the caller through a capture by const reference, and a body of the frame
// door of a list of linear ends takes the ends as its parameter.  The linear
// values stay with the caller, and the caller ends them after the doors.
void test_doors_take_a_body_that_borrows_a_linear_value() {
    LinearEnd end{5};
    LinearEnd const& reader = end;
    FixedArray<int, 2> counts{};
    counts.with_element(index<0>(), [&reader](int& count) noexcept { count = reader.handle; });
    int const size =
        counts.with_mut([&reader](auto run) noexcept { return static_cast<int>(run.size()) + reader.handle; });
    ::foundation::core::FixedList<LinearEnd, 1> ends;
    static_cast<void>(ends.append(LinearEnd{2}).expect("the list has room"));
    auto [first, back] = with_mut(std::move(ends), [](auto run) noexcept { return run.size() == 1 ? 1 : 0; });
    CRUCIBLE_TEST_ASSERT(kTest, size == 7 && first == 1 && counts.at(index<0>()) == 5);
    LinearEnd taken = back.take_last().expect("the list holds one end");
    CRUCIBLE_TEST_ASSERT(kTest, taken.handle == 2);
    ::foundation::linear::drop(std::move(taken));
    std::move(back).expect_empty("the test took the end out of the list");
    ::foundation::linear::drop(std::move(end));
}

void test_fixed_array_view_fill_and_copy() {
    FixedArray<std::uint32_t, 8> target;
    FixedArray<std::uint32_t, 8> source{1, 2, 3, 4, 5, 6, 7, 8};
    target.with_mut([](auto run) noexcept { ::foundation::core::fill(std::move(run), std::uint32_t{0xAB}); });
    for (std::uint32_t const value : target) {
        CRUCIBLE_TEST_ASSERT(kTest, value == 0xAB);
    }
    target.with_mut([&source](auto run) noexcept { ::foundation::core::copy(std::move(run), source.view()); });
    CRUCIBLE_TEST_ASSERT(kTest, target == source);
    View<std::uint32_t const, 8> const elements = target.view();
    CRUCIBLE_TEST_ASSERT(kTest, elements.at(index<7>()) == 8);
    std::uint32_t sum = 0;
    for (Index<8> const position : elements.indices()) {
        sum += elements.at(position);
    }
    CRUCIBLE_TEST_ASSERT(kTest, sum == 36);
    // A window of the run of a frame writes the elements of the array.
    target.with_mut([](auto run) noexcept {
        ::foundation::core::fill(::foundation::core::take_window(std::move(run), 2, 3).expect("three elements fit"),
                                 std::uint32_t{0});
    });
    CRUCIBLE_TEST_ASSERT(kTest, target.at(index<1>()) == 2 && target.at(index<2>()) == 0 && target.at(index<4>()) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, target.at(index<5>()) == 6);
    CRUCIBLE_TEST_ASSERT(kTest, !(target == source));
}

// prefix_up_to gives the first count elements of a View, or each element for
// a count past the size: the window that a read door filled.
// take_prefix_up_to gives the same part of the run of a frame as a ViewMut,
// and a write through it writes the elements of the array.
void test_prefix_up_to_is_total() {
    FixedArray<std::uint32_t, 6> numbers{1, 2, 3, 4, 5, 6};
    std::size_t volatile seed = 4;
    std::size_t const filled = seed;
    View<std::uint32_t const> const elements = numbers.view();
    CRUCIBLE_TEST_ASSERT(kTest, elements.prefix_up_to(filled).size() == 4);
    CRUCIBLE_TEST_ASSERT(kTest, elements.prefix_up_to(filled * 10).size() == 6);
    CRUCIBLE_TEST_ASSERT(kTest, elements.prefix_up_to(0).size() == 0);
    std::size_t const taken = numbers.with_mut([filled](auto run) noexcept {
        auto prefix = ::foundation::core::take_prefix_up_to(std::move(run), filled);
        std::size_t const count = prefix.size();
        ::foundation::core::fill(std::move(prefix), std::uint32_t{0});
        return count;
    });
    CRUCIBLE_TEST_ASSERT(kTest, taken == 4);
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(index<3>()) == 0 && numbers.at(index<4>()) == 5);
    std::size_t const whole = numbers.with_mut(
        [](auto run) noexcept { return ::foundation::core::take_prefix_up_to(std::move(run), 1000).size(); });
    CRUCIBLE_TEST_ASSERT(kTest, whole == 6);
}

// erase_brand gives the writer of the same run and extent on the erased
// identity, and a write through it writes the elements of the array.  The
// runs of two frames of one element type and extent give one erased type.
void test_erase_brand_keeps_the_run() {
    FixedArray<std::uint32_t, 4> first{1, 2, 3, 4};
    FixedArray<std::uint32_t, 4> second{5, 6, 7, 8};
    first.with_mut([&second](auto first_run) noexcept {
        second.with_mut([&first_run](auto second_run) noexcept {
            auto erased_first = ::foundation::core::erase_brand(std::move(first_run));
            auto erased_second = ::foundation::core::erase_brand(std::move(second_run));
            static_assert(std::is_same_v<decltype(erased_first), decltype(erased_second)>);
            static_assert(decltype(erased_first)::extent == 4);
            static_assert(!::foundation::brand::IsFreshBrand<typename decltype(erased_first)::brand_type>);
            CRUCIBLE_TEST_ASSERT(kTest, erased_first.size() == 4 && erased_second.size() == 4);
            ::foundation::core::copy(std::move(erased_first), erased_second.view());
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, first.at(index<0>()) == 5 && first.at(index<3>()) == 8);
    CRUCIBLE_TEST_ASSERT(kTest, second.at(index<0>()) == 5);
}

// get gives a Ref of the element below the count, and no value at or past
// the count, for a View of each extent and for a FixedArray.
void test_get_reads_below_the_count() {
    FixedArray<int, 4> numbers{4, 8, 15, 16};
    FixedArray<int, 4> const& fixed = numbers;
    numbers.with_mut([](auto run) noexcept {
        View<int const> const elements = run.view();
        CRUCIBLE_TEST_ASSERT(kTest, *elements.get(2).expect("the position is below the count") == 15);
        CRUCIBLE_TEST_ASSERT(kTest, elements.get(4).is_none());
        CRUCIBLE_TEST_ASSERT(kTest, elements.get(~std::size_t{0}).is_none());
        auto first = run.get_mut(0).expect("the position is below the count");
        *first = 5;
    });
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(index<0>()) == 5);

    View<int const, 4> const fixed_view = fixed.view();
    CRUCIBLE_TEST_ASSERT(kTest, *fixed_view.get(3).expect("the position is below the count") == 16);
    CRUCIBLE_TEST_ASSERT(kTest, fixed_view.get(4).is_none());

    numbers.with_mut([](auto run) noexcept {
        auto second = run.get_mut(1).expect("the position is below the count");
        *second = 9;
    });
    CRUCIBLE_TEST_ASSERT(kTest, numbers.at(index<1>()) == 9);
    CRUCIBLE_TEST_ASSERT(kTest, *fixed.get(1).expect("the position is below the count") == 9);
    CRUCIBLE_TEST_ASSERT(kTest, fixed.get(4).is_none());

    // A Ref from get reaches the element of the array.  Each call of get has
    // a fresh brand, and same_object accepts two brands.
    auto const third = numbers.get(2).expect("the position is below the count");
    CRUCIBLE_TEST_ASSERT(
        kTest, ::foundation::core::same_object(third, numbers.get(2).expect("the position is below the count")));

    View<int const> const empty{};
    CRUCIBLE_TEST_ASSERT(kTest, empty.get(0).is_none());
}

void test_fixed_array_of_a_type_with_no_default() {
    FixedArray<NoDefault, 3> const values{NoDefault{1}, NoDefault{2}, NoDefault{3}};
    int total = 0;
    for (NoDefault const& value : values) {
        total += value.value;
    }
    CRUCIBLE_TEST_ASSERT(kTest, total == 6);
    FixedArray<NoDefault, 3> const same{NoDefault{1}, NoDefault{2}, NoDefault{3}};
    CRUCIBLE_TEST_ASSERT(kTest, values == same);
}

// The sum of the elements of an array, for a constant evaluation.
constexpr std::uint32_t sum_of(FixedArray<std::uint32_t, 4> const& values) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const value : values) {
        sum += value;
    }
    return sum;
}

// The View of a FixedArray comes from a reference to its array, with no
// compare with a null pointer.  The build sets -fno-delete-null-pointer-checks,
// and with it GCC 16 does not fold the compare of the address of a
// block-scope static object with a null pointer in a constant evaluation.  A
// loop over a static FixedArray is then a constant expression only when the
// View has no such compare.
void test_fixed_array_view_of_a_static_is_constant() {
    static constexpr FixedArray<std::uint32_t, 4> values{1, 2, 3, 4};
    static constexpr std::uint32_t sum = sum_of(values);
    static_assert(sum == 10);
    CRUCIBLE_TEST_ASSERT(kTest, sum_of(values) == 10);
}

// The property: random writes at checked indexes give the elements of the
// same writes into a std::array.
void test_fixed_array_agrees_with_std_array() {
    ::crucible::test::PhiloxStream stream{0xA77A1E5E0000B002u};
    FixedArray<std::uint64_t, 64> array;
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::array is the model of the writes of a FixedArray")
    std::array<std::uint64_t, 64> model{};
    for (int trial = 0; trial < 8192; ++trial) {
        std::size_t const raw = stream.below(80);
        std::uint64_t const value = stream.next_wide();
        for (Index<64> const position : Index<64>::check(raw)) {
            array.with_element(position, [value](std::uint64_t& element) noexcept { element = value; });
        }
        if (raw < model.size()) model[raw] = value;
    }
    std::size_t position = 0;
    for (std::uint64_t const value : array) {
        CRUCIBLE_TEST_ASSERT(kTest, value == model[position]);
        ++position;
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_index_check_agrees_with_the_model();
    test_index_range_visits_each_index_in_order();
    test_index_cursor_at_the_end_aborts(test);
    test_fixed_array_starts_with_zeros();
    test_large_fixed_array_value_initializes();
    test_fixed_array_reads_at_proven_indexes();
    test_fixed_array_element_door_writes_in_place();
    test_fixed_array_value_door_builds_in_place();
    test_fixed_array_value_doors_write_one_element();
    test_fixed_array_value_doors_write_the_run();
    test_fixed_array_copy_doors_take_their_own_run();
    test_build_fixed_array_gives_a_new_array();
    test_doors_take_a_body_that_borrows_a_linear_value();
    test_fixed_array_view_fill_and_copy();
    test_prefix_up_to_is_total();
    test_erase_brand_keeps_the_run();
    test_get_reads_below_the_count();
    test_fixed_array_of_a_type_with_no_default();
    test_fixed_array_view_of_a_static_is_constant();
    test_fixed_array_agrees_with_std_array();
    ::fixy::check::pass(kTest, "test_core_array: all tests passed\n");
    return 0;
}
