// Tests of the branded scope of the Region family: index, at, indices and
// enumerate of the BrandedView that mint_branded_scope gives its body.  The
// property compares the scope with the reads of the same run through a
// proven Index of the array.

#include <foundation/core/Branded.h>

#include "../test_assert.h"
#include "abort_probe.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::mint_branded_scope;
using ::foundation::core::View;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void test_scope_reads_each_element() {
    FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    View<int> const tail = values.view().window(2, 4).expect("four elements fit");
    int const total = mint_branded_scope(tail, [](auto view) noexcept {
        int sum = 0;
        for (auto const index : view.indices()) {
            sum += view.at(index);
        }
        for (auto const [index, element] : view.enumerate()) {
            *element *= static_cast<int>(index.raw()) + 1;
        }
        return view.index(4).is_none() ? sum : -1;
    });
    assert(total == 3 + 4 + 5 + 6);
    assert(values == (FixedArray<int, 6>{1, 2, 3, 8, 15, 24}));
}

void test_scope_of_an_empty_view() {
    View<int const> const empty{};
    std::size_t const turns = mint_branded_scope(empty, [](auto view) noexcept {
        std::size_t count = 0;
        for ([[maybe_unused]] auto const index : view.indices()) {
            ++count;
        }
        return view.index(0).is_none() && view.size() == 0 ? count : std::size_t{1};
    });
    assert(turns == 0);
}

// The property: for random runs and random windows, each index of the scope
// reads the element of the model at the same position.
void test_scope_agrees_with_the_index_model() {
    ::crucible::test::PhiloxStream stream{0x6B7A8C9D0E1F2031u};
    Run run;
    for (int trial = 0; trial < 512; ++trial) {
        for (std::uint64_t& value : run) {
            value = stream.next_wide();
        }
        std::size_t const offset = stream.below(run_size + 1);
        std::size_t const size = stream.below(run_size - offset + 1);
        View<std::uint64_t const> const window = run.view().window(offset, size).expect("the window fits");
        bool const is_equal = mint_branded_scope(window, [&run, offset, size](auto view) noexcept {
            bool equal = view.size() == size;
            std::size_t position = 0;
            for (auto const [index, element] : view.enumerate()) {
                equal = equal && index.raw() == position && *element == element_at(run, offset + position);
                equal = equal && view.at(index) == *element;
                ++position;
            }
            return equal && position == size && view.index(size).is_none();
        });
        assert(is_equal);
    }
}

// A cursor of a scope never leaves its loop: a read at the end and a step
// past the end end the process in each build.
void test_cursors_at_the_end_abort() {
    FixedArray<int, 3> values{1, 2, 3};
    bool const aborts_each = mint_branded_scope(values.view(), [](auto view) noexcept {
        bool const index_read = ::crucible::test::aborts([&view] { static_cast<void>(*view.indices().end()); });
        bool const index_step = ::crucible::test::aborts([&view] {
            auto cursor = view.indices().end();
            ++cursor;
        });
        bool const pair_read = ::crucible::test::aborts([&view] { static_cast<void>(*view.enumerate().end()); });
        bool const pair_step = ::crucible::test::aborts([&view] {
            auto cursor = view.enumerate().end();
            ++cursor;
        });
        return index_read && index_step && pair_read && pair_step;
    });
    assert(aborts_each);
}

}  // namespace

int main() {
    test_scope_reads_each_element();
    test_scope_of_an_empty_view();
    test_scope_agrees_with_the_index_model();
    test_cursors_at_the_end_abort();
    crucible::test::pass("test_core_branded: all tests passed\n");
    return 0;
}
