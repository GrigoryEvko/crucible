// Tests of the iteration verbs of the Region family: enumerate(), reverse(),
// windows<W>() and chunks<W>() of a View and of a FixedArray, and zip and
// try_zip of two walks of Views.  Each property compares a verb with a loop
// over the proven indexes of the same array, which is the model.

#include <foundation/core/Array.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::LengthMismatch;
using ::foundation::core::View;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void fill_with_stream(Run& run, ::crucible::test::PhiloxStream& stream) {
    for (auto const index : run.indices()) {
        run.set(index, stream.next_wide());
    }
}

void test_reverse_reads_from_the_last_element() {
    FixedArray<int, 5> const values{1, 2, 3, 4, 5};
    int expected = 5;
    for (int const value : values.reverse()) {
        CRUCIBLE_TEST_ASSERT(kTest, value == expected);
        --expected;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected == 0);
    auto [numbers, numbers_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 3>,
                                                                       FixedArray<int, 3>{1, 2, 3});
    numbers.with_mut(numbers_key, [](auto run) noexcept {
        std::size_t position = run.size();
        for (int const value : run.view().reverse()) {
            --position;
            auto element = run.get_mut(position).expect("the position is below the count");
            *element = value * 10;
        }
    });
    FixedArray<int, 3> const expected_numbers{10, 20, 30};
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::equal(numbers.view(), expected_numbers.view()));
}

void test_windows_and_chunks_of_an_array() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    int count = 0;
    int first_total = 0;
    for (View<int const, 4> const run : values.windows<4>()) {
        first_total += run.at(index<0>());
        ++count;
    }
    CRUCIBLE_TEST_ASSERT(kTest, count == 3 && first_total == 1 + 2 + 3);
    auto const runs = values.view().chunks<4>();
    int chunk_count = 0;
    for (View<int const, 4> const run : runs) {
        CRUCIBLE_TEST_ASSERT(kTest, run.at(index<3>()) == 4);
        ++chunk_count;
    }
    CRUCIBLE_TEST_ASSERT(kTest, chunk_count == 1);
    CRUCIBLE_TEST_ASSERT(kTest,
                         ::foundation::core::equal(runs.rest(), values.view().window(4, 2).expect("two elements fit")));
}

void test_enumerate_gives_each_index_with_its_element() {
    FixedArray<int, 5> const values{10, 20, 30, 40, 50};
    std::size_t expected_position = 0;
    for (auto const [position, element] : values.enumerate()) {
        CRUCIBLE_TEST_ASSERT(kTest, position.raw() == expected_position);
        CRUCIBLE_TEST_ASSERT(kTest, *element == values.at(position));
        ++expected_position;
    }
    CRUCIBLE_TEST_ASSERT(kTest, expected_position == 5);
    FixedArray<int, 4> const numbers = ::foundation::core::build_fixed_array<int, 4>([](auto run) noexcept {
        for (auto const [position, element] : run.view().enumerate()) {
            auto writer = run.get_mut(position.raw()).expect("the position is below the count");
            *writer = *element + static_cast<int>(position.raw()) * 3;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, numbers == (FixedArray<int, 4>{0, 3, 6, 9}));
}

void test_zip_of_one_fixed_extent() {
    FixedArray<int, 4> const left{1, 2, 3, 4};
    FixedArray<int, 4> const right{10, 20, 30, 40};
    int total = 0;
    for (auto const [first, second] : left.view().walk().zip(right.view().walk())) {
        total += *first * *second;
    }
    CRUCIBLE_TEST_ASSERT(kTest, total == 10 + 40 + 90 + 160);
    FixedArray<int, 4> const target = ::foundation::core::build_fixed_array<int, 4>([&left](auto run) noexcept {
        std::size_t position = 0;
        for (auto const [destination, source] : run.view().walk().zip(left.view().walk())) {
            auto writer = run.get_mut(position).expect("the position is below the count");
            *writer = *destination + *source + 1;
            ++position;
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, target == (FixedArray<int, 4>{2, 3, 4, 5}));
}

void test_zip_of_a_dynamic_extent_compares_the_counts() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    View<int const> const head = values.view().window(0, 3).expect("three elements fit");
    View<int const> const tail = values.view().window(3, 3).expect("three elements fit");
    View<int const> const longer = values.view().window(1, 4).expect("four elements fit");
    CRUCIBLE_TEST_ASSERT(kTest, head.walk().try_zip(longer.walk()).is_err());
    CRUCIBLE_TEST_ASSERT(kTest, values.view().walk().try_zip(head.walk()).is_err());
    auto const pairs = head.walk().try_zip(tail.walk()).expect("the two windows hold three elements");
    CRUCIBLE_TEST_ASSERT(kTest, pairs.size() == 3);
    int total = 0;
    for (auto const [first, second] : pairs) {
        CRUCIBLE_TEST_ASSERT(kTest, *second == *first + 3);
        total += *first;
    }
    CRUCIBLE_TEST_ASSERT(kTest, total == 1 + 2 + 3);
    bool const mismatch_matched =
        head.walk().try_zip(longer.walk()).match([](auto) { return false; }, [](LengthMismatch) { return true; });
    CRUCIBLE_TEST_ASSERT(kTest, mismatch_matched);
    View<int const> const empty{};
    CRUCIBLE_TEST_ASSERT(kTest,
                         empty.walk().try_zip(empty.walk()).expect("two empty Views hold zero elements").size() == 0);
}

// The property: for random runs and random counts, reverse() visits the
// elements of the model from the last one, windows<4>() starts a run at each
// position of the model, and chunks<5>() starts a run at each fifth
// position, with the rest after the last whole chunk.
void test_verbs_agree_with_the_index_model() {
    ::crucible::test::PhiloxStream stream{0x17E2A7E50000E005u};
    Run run;
    for (int trial = 0; trial < 1024; ++trial) {
        fill_with_stream(run, stream);
        std::size_t const size = stream.below(run_size + 1);
        View<std::uint64_t const> const elements = run.view().window(0, size).expect("the size is at most the run");

        std::size_t position = size;
        for (std::uint64_t const value : elements.reverse()) {
            --position;
            CRUCIBLE_TEST_ASSERT(kTest, value == element_at(run, position));
        }
        CRUCIBLE_TEST_ASSERT(kTest, position == 0);

        std::size_t start = 0;
        for (View<std::uint64_t const, 4> const window : elements.windows<4>()) {
            CRUCIBLE_TEST_ASSERT(kTest, window.at(index<0>()) == element_at(run, start));
            CRUCIBLE_TEST_ASSERT(kTest, window.at(index<3>()) == element_at(run, start + 3));
            ++start;
        }
        CRUCIBLE_TEST_ASSERT(kTest, start == (size < 4 ? 0 : size - 3));

        auto const chunks = elements.chunks<5>();
        std::size_t chunk_start = 0;
        for (View<std::uint64_t const, 5> const chunk : chunks) {
            for (Index<5> const offset : chunk.indices()) {
                CRUCIBLE_TEST_ASSERT(kTest, chunk.at(offset) == element_at(run, chunk_start + offset.raw()));
            }
            chunk_start += 5;
        }
        CRUCIBLE_TEST_ASSERT(kTest, chunk_start == size - size % 5);
        CRUCIBLE_TEST_ASSERT(kTest, chunks.rest().size() == size % 5);
        CRUCIBLE_TEST_ASSERT(
            kTest,
            ::foundation::core::equal(
                chunks.rest(), elements.window(chunk_start, size % 5).expect("the rest is the tail of the run")));

        std::size_t enumerated = 0;
        for (auto const [offset, element] : run.enumerate()) {
            CRUCIBLE_TEST_ASSERT(kTest, offset.raw() == enumerated && *element == element_at(run, enumerated));
            ++enumerated;
        }
        CRUCIBLE_TEST_ASSERT(kTest, enumerated == run_size);

        std::size_t const shift = stream.below(run_size - size + 1);
        View<std::uint64_t const> const shifted =
            run.view().window(shift, size).expect("the shifted window is inside the run");
        std::size_t paired = 0;
        for (auto const [first, second] :
             elements.walk().try_zip(shifted.walk()).expect("the two windows hold size elements")) {
            CRUCIBLE_TEST_ASSERT(kTest,
                                 *first == element_at(run, paired) && *second == element_at(run, shift + paired));
            ++paired;
        }
        CRUCIBLE_TEST_ASSERT(kTest, paired == size);
        if (size != 0) {
            View<std::uint64_t const> const shorter = elements.window(1, size - 1).expect("the tail of a window fits");
            CRUCIBLE_TEST_ASSERT(kTest, elements.walk().try_zip(shorter.walk()).is_err());
        }
    }
}

// A cursor of a verb never leaves its loop: a read at the end and a step
// past the end end the process in each build.  Each one runs in a child
// through expect_dies of the Check family.
void test_cursors_at_the_end_abort(::fixy::TestRunnerCtx const& test) {
    using ::fixy::check::expect_dies;
    FixedArray<int, 4> const values{1, 2, 3, 4};
    expect_dies(
        test, [&values] noexcept { static_cast<void>(*values.reverse().end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a reverse loop over a View"});
    expect_dies(
        test,
        [&values] noexcept {
            auto cursor = values.reverse().end();
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a reverse loop over a View"});
    expect_dies(
        test, [&values] noexcept { static_cast<void>(*values.windows<2>().end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a loop over the windows of a View"});
    expect_dies(
        test,
        [&values] noexcept {
            auto cursor = values.windows<4>().begin();
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a loop over the windows of a View"});
    expect_dies(
        test, [&values] noexcept { static_cast<void>(*values.chunks<3>().end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a loop over the chunks of a View"});
    expect_dies(
        test,
        [&values] noexcept {
            auto cursor = values.chunks<4>().begin();
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a loop over the chunks of a View"});
    expect_dies(
        test, [&values] noexcept { static_cast<void>(*values.enumerate().end()); },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of an enumerate loop over a View"});
    expect_dies(
        test,
        [&values] noexcept {
            auto cursor = values.enumerate().end();
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of an enumerate loop over a View"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const pairs = values.view().walk().zip(values.view().walk());
            static_cast<void>(*pairs.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a zip walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const pairs = values.view().walk().zip(values.view().walk());
            auto cursor = pairs.end();
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a zip walk"});
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_reverse_reads_from_the_last_element();
    test_windows_and_chunks_of_an_array();
    test_enumerate_gives_each_index_with_its_element();
    test_zip_of_one_fixed_extent();
    test_zip_of_a_dynamic_extent_compares_the_counts();
    test_verbs_agree_with_the_index_model();
    test_cursors_at_the_end_abort(test);
    ::fixy::check::pass(kTest, "test_core_iteration: all tests passed\n");
    return 0;
}
