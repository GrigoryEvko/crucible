// Tests of the iteration verbs of the Region family: enumerate(), reverse(),
// windows<W>() and chunks<W>() of a View and of a FixedArray, and zip of two
// Views.  Each property compares a verb with a loop over the proven indexes
// of the same array, which is the model.

#include <foundation/core/Array.h>

#include "../test_assert.h"
#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::index;
using ::foundation::core::LengthMismatch;
using ::foundation::core::View;
using ::foundation::core::zip;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void fill_with_stream(Run& run, ::crucible::test::PhiloxStream& stream) {
    for (std::uint64_t& value : run) {
        value = stream.next_wide();
    }
}

void test_reverse_reads_from_the_last_element() {
    FixedArray<int, 5> const values{1, 2, 3, 4, 5};
    int expected = 5;
    for (int const value : values.reverse()) {
        assert(value == expected);
        --expected;
    }
    assert(expected == 0);
    FixedArray<int, 3> numbers{1, 2, 3};
    for (int& value : numbers.reverse()) {
        value *= 10;
    }
    assert(numbers == (FixedArray<int, 3>{10, 20, 30}));
}

void test_windows_and_chunks_of_an_array() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    int count = 0;
    int first_total = 0;
    for (View<int const, 4> const run : values.windows<4>()) {
        first_total += run.at(index<0>());
        ++count;
    }
    assert(count == 3 && first_total == 1 + 2 + 3);
    auto const runs = values.view().chunks<4>();
    int chunk_count = 0;
    for (View<int const, 4> const run : runs) {
        assert(run.at(index<3>()) == 4);
        ++chunk_count;
    }
    assert(chunk_count == 1);
    assert(::foundation::core::equal(runs.rest(), values.view().window(4, 2).expect("two elements fit")));
}

void test_enumerate_gives_each_index_with_its_element() {
    FixedArray<int, 5> const values{10, 20, 30, 40, 50};
    std::size_t expected_position = 0;
    for (auto const [position, element] : values.enumerate()) {
        assert(position.raw() == expected_position);
        assert(*element == values.at(position));
        ++expected_position;
    }
    assert(expected_position == 5);
    FixedArray<int, 4> numbers{};
    for (auto const [position, element] : numbers.enumerate()) {
        *element = static_cast<int>(position.raw()) * 3;
    }
    assert(numbers == (FixedArray<int, 4>{0, 3, 6, 9}));
}

void test_zip_of_one_fixed_extent() {
    FixedArray<int, 4> const left{1, 2, 3, 4};
    FixedArray<int, 4> const right{10, 20, 30, 40};
    int total = 0;
    for (auto const [first, second] : zip(left.view(), right.view())) {
        total += *first * *second;
    }
    assert(total == 10 + 40 + 90 + 160);
    FixedArray<int, 4> target{};
    for (auto const [destination, source] : zip(target.view(), left.view())) {
        *destination = *source + 1;
    }
    assert(target == (FixedArray<int, 4>{2, 3, 4, 5}));
}

void test_zip_of_a_dynamic_extent_compares_the_counts() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    View<int const> const head = values.view().window(0, 3).expect("three elements fit");
    View<int const> const tail = values.view().window(3, 3).expect("three elements fit");
    View<int const> const longer = values.view().window(1, 4).expect("four elements fit");
    assert(zip(head, longer).is_err());
    assert(zip(values.view(), head).is_err());
    auto const pairs = zip(head, tail).expect("the two windows hold three elements");
    assert(pairs.size() == 3);
    int total = 0;
    for (auto const [first, second] : pairs) {
        assert(*second == *first + 3);
        total += *first;
    }
    assert(total == 1 + 2 + 3);
    bool const mismatch_matched =
        zip(head, longer).match([](auto) { return false; }, [](LengthMismatch) { return true; });
    assert(mismatch_matched);
    View<int const> const empty{};
    assert(zip(empty, empty).expect("two empty Views hold zero elements").size() == 0);
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
            assert(value == element_at(run, position));
        }
        assert(position == 0);

        std::size_t start = 0;
        for (View<std::uint64_t const, 4> const window : elements.windows<4>()) {
            assert(window.at(index<0>()) == element_at(run, start));
            assert(window.at(index<3>()) == element_at(run, start + 3));
            ++start;
        }
        assert(start == (size < 4 ? 0 : size - 3));

        auto const chunks = elements.chunks<5>();
        std::size_t chunk_start = 0;
        for (View<std::uint64_t const, 5> const chunk : chunks) {
            for (Index<5> const offset : chunk.indices()) {
                assert(chunk.at(offset) == element_at(run, chunk_start + offset.raw()));
            }
            chunk_start += 5;
        }
        assert(chunk_start == size - size % 5);
        assert(chunks.rest().size() == size % 5);
        assert(::foundation::core::equal(
            chunks.rest(), elements.window(chunk_start, size % 5).expect("the rest is the tail of the run")));

        std::size_t enumerated = 0;
        for (auto const [offset, element] : run.enumerate()) {
            assert(offset.raw() == enumerated && *element == element_at(run, enumerated));
            ++enumerated;
        }
        assert(enumerated == run_size);

        std::size_t const shift = stream.below(run_size - size + 1);
        View<std::uint64_t const> const shifted =
            run.view().window(shift, size).expect("the shifted window is inside the run");
        std::size_t paired = 0;
        for (auto const [first, second] : zip(elements, shifted).expect("the two windows hold size elements")) {
            assert(*first == element_at(run, paired) && *second == element_at(run, shift + paired));
            ++paired;
        }
        assert(paired == size);
        if (size != 0) {
            View<std::uint64_t const> const shorter = elements.window(1, size - 1).expect("the tail of a window fits");
            assert(zip(elements, shorter).is_err());
        }
    }
}

// A cursor of a verb never leaves its loop: a read at the end and a step
// past the end end the process in each build.
void test_cursors_at_the_end_abort() {
    FixedArray<int, 4> const values{1, 2, 3, 4};
    bool const reverse_read_aborts =
        ::crucible::test::aborts([&values] { static_cast<void>(*values.reverse().end()); });
    bool const reverse_step_aborts = ::crucible::test::aborts([&values] {
        auto cursor = values.reverse().end();
        ++cursor;
    });
    bool const window_read_aborts =
        ::crucible::test::aborts([&values] { static_cast<void>(*values.windows<2>().end()); });
    bool const window_step_aborts = ::crucible::test::aborts([&values] {
        auto cursor = values.windows<4>().begin();
        ++cursor;
        ++cursor;
    });
    bool const chunk_read_aborts =
        ::crucible::test::aborts([&values] { static_cast<void>(*values.chunks<3>().end()); });
    bool const chunk_step_aborts = ::crucible::test::aborts([&values] {
        auto cursor = values.chunks<4>().begin();
        ++cursor;
        ++cursor;
    });
    bool const enumerate_read_aborts =
        ::crucible::test::aborts([&values] { static_cast<void>(*values.enumerate().end()); });
    bool const enumerate_step_aborts = ::crucible::test::aborts([&values] {
        auto cursor = values.enumerate().end();
        ++cursor;
    });
    bool const zip_read_aborts =
        ::crucible::test::aborts([&values] { static_cast<void>(*zip(values.view(), values.view()).end()); });
    bool const zip_step_aborts = ::crucible::test::aborts([&values] {
        auto cursor = zip(values.view(), values.view()).end();
        ++cursor;
    });
    assert(reverse_read_aborts && reverse_step_aborts && window_read_aborts && window_step_aborts);
    assert(chunk_read_aborts && chunk_step_aborts && enumerate_read_aborts && enumerate_step_aborts);
    assert(zip_read_aborts && zip_step_aborts);
}

}  // namespace

int main() {
    test_reverse_reads_from_the_last_element();
    test_windows_and_chunks_of_an_array();
    test_enumerate_gives_each_index_with_its_element();
    test_zip_of_one_fixed_extent();
    test_zip_of_a_dynamic_extent_compares_the_counts();
    test_verbs_agree_with_the_index_model();
    test_cursors_at_the_end_abort();
    crucible::test::pass("test_core_iteration: all tests passed\n");
    return 0;
}
