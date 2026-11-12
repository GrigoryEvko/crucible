// Tests of the iteration verbs of the Region family: reverse(), windows<W>()
// and chunks<W>() of a View and of a FixedArray.  Each property compares a
// verb with a loop over the proven indexes of the same array, which is the
// model.

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
using ::foundation::core::View;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void fill_with_stream(Run& run, ::foundation::test::PhiloxStream& stream) {
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

// The property: for random runs and random counts, reverse() visits the
// elements of the model from the last one, windows<4>() starts a run at each
// position of the model, and chunks<5>() starts a run at each fifth
// position, with the rest after the last whole chunk.
void test_verbs_agree_with_the_index_model() {
    ::foundation::test::PhiloxStream stream{0x17E2A7E50000E005u};
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
    }
}

// A cursor of a verb never leaves its loop: a read at the end and a step
// past the end end the process in each build.
void test_cursors_at_the_end_abort() {
    FixedArray<int, 4> const values{1, 2, 3, 4};
    bool const reverse_read_aborts =
        ::foundation::test::aborts([&values] { static_cast<void>(*values.reverse().end()); });
    bool const reverse_step_aborts = ::foundation::test::aborts([&values] {
        auto cursor = values.reverse().end();
        ++cursor;
    });
    bool const window_read_aborts =
        ::foundation::test::aborts([&values] { static_cast<void>(*values.windows<2>().end()); });
    bool const window_step_aborts = ::foundation::test::aborts([&values] {
        auto cursor = values.windows<4>().begin();
        ++cursor;
        ++cursor;
    });
    bool const chunk_read_aborts =
        ::foundation::test::aborts([&values] { static_cast<void>(*values.chunks<3>().end()); });
    bool const chunk_step_aborts = ::foundation::test::aborts([&values] {
        auto cursor = values.chunks<4>().begin();
        ++cursor;
        ++cursor;
    });
    assert(reverse_read_aborts && reverse_step_aborts && window_read_aborts && window_step_aborts);
    assert(chunk_read_aborts && chunk_step_aborts);
}

}  // namespace

int main() {
    test_reverse_reads_from_the_last_element();
    test_windows_and_chunks_of_an_array();
    test_verbs_agree_with_the_index_model();
    test_cursors_at_the_end_abort();
    crucible::test::pass("test_core_iteration: all tests passed\n");
    return 0;
}
