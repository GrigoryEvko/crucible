// Tests of the Walk family of foundation/core/Walk.h: the adapters and the
// terminals of a walk over a View.  Each property compares a chain with a
// loop over the proven indexes of the same array, which is the model.

#include <foundation/core/Array.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::ElementCount;
using ::foundation::core::FixedArray;
using ::foundation::core::Index;
using ::foundation::core::LengthMismatch;
using ::foundation::core::View;

constexpr std::size_t run_size = 64;

using Run = FixedArray<std::uint64_t, run_size>;

// The element at a position below the size of the run.
[[nodiscard]] std::uint64_t element_at(Run const& run, std::size_t position) {
    return run.at(Index<run_size>::check(position).expect("the position is below the size of the run"));
}

void fill_with_stream(Run& run, ::crucible::test::PhiloxStream& stream) {
    for (std::uint64_t& value : run) {
        value = stream.next_wide() % 1000;
    }
}

[[nodiscard]] constexpr bool is_kept(std::uint64_t const& value) noexcept { return value % 3 != 0; }

// A float fold adds in the left order, so the bits are the bits of the plain
// loop.  The order (1e8 + 1) - 1e8 + 1 gives 1, and a different order gives 2.
void test_float_fold_runs_left_to_right() {
    FixedArray<float, 4> const values{1.0e8f, 1.0f, -1.0e8f, 1.0f};
    float const folded =
        values.view().walk().fold(0.0f, [](float sum, float const& value) noexcept { return sum + value; });
    float plain = 0.0f;
    for (float const value : values) {
        plain += value;
    }
    using ::foundation::core::bit_cast;
    assert(bit_cast<std::uint32_t>(folded) == bit_cast<std::uint32_t>(plain));
    assert(bit_cast<std::uint32_t>(folded) == bit_cast<std::uint32_t>(1.0f));
}

// The cursor loop over a walk of a View of mutable elements writes each one.
void test_cursor_loop_writes_through_the_walk() {
    FixedArray<int, 5> values{1, 2, 3, 4, 5};
    for (int& value : values.view().walk().skip(ElementCount{1}).limit(ElementCount{3})) {
        value *= 10;
    }
    assert(values == (FixedArray<int, 5>{1, 20, 30, 40, 5}));
    FixedArray<int, 3> target{};
    FixedArray<int, 3> const source{7, 8, 9};
    for (auto const [destination, origin] : target.view().walk().zip(source.view().walk())) {
        *destination = *origin + 1;
    }
    assert(target == (FixedArray<int, 3>{8, 9, 10}));
}

void test_try_zip_compares_the_counts() {
    FixedArray<int, 6> const values{1, 2, 3, 4, 5, 6};
    View<int const> const head = values.view().window(0, 3).expect("three elements fit");
    View<int const> const tail = values.view().window(3, 3).expect("three elements fit");
    View<int const> const longer = values.view().window(1, 4).expect("four elements fit");
    assert(head.walk().try_zip(longer.walk()).is_err());
    bool const is_mismatch =
        head.walk()
            .try_zip(longer.walk())
            .match([](auto) noexcept { return false; }, [](LengthMismatch) noexcept { return true; });
    assert(is_mismatch);
    auto const pairs = head.walk().try_zip(tail.walk()).expect("the two windows hold three elements");
    assert(pairs.size() == 3);
    int total = 0;
    for (auto const [first, second] : pairs) {
        assert(*second == *first + 3);
        total += *first;
    }
    assert(total == 1 + 2 + 3);
    View<int const> const empty{};
    assert(empty.walk().try_zip(empty.walk()).expect("two empty runs hold zero elements").size() == 0);
}

// The property: for random runs and counts, each chain gives the value of the
// loop over the indexes of the model.
void test_chains_agree_with_the_index_model() {
    ::crucible::test::PhiloxStream stream{0x57A1C0DE0000E804u};
    Run run;
    Run other;
    for (int trial = 0; trial < 1024; ++trial) {
        fill_with_stream(run, stream);
        fill_with_stream(other, stream);
        std::size_t const size = stream.below(run_size + 1);
        std::size_t const limit = stream.below(run_size + 2);
        std::size_t const skipped = stream.below(run_size + 2);
        std::uint64_t const wanted = stream.below(1000);
        View<std::uint64_t const> const elements = run.view().window(0, size).expect("the size is at most the run");
        View<std::uint64_t const> const others = other.view().window(0, size).expect("the size is at most the run");

        std::uint64_t model_squares = 0;
        std::size_t model_kept = 0;
        std::uint64_t model_kept_prefix = 0;
        std::size_t kept_so_far = 0;
        std::uint64_t model_skipped = 0;
        std::uint64_t model_dot = 0;
        bool is_wanted_found = false;
        bool is_each_below = true;
        for (std::size_t position = 0; position < size; ++position) {
            std::uint64_t const value = element_at(run, position);
            model_squares = model_squares * 31 + value * value;
            if (is_kept(value)) {
                ++model_kept;
                if (kept_so_far < limit) {
                    model_kept_prefix = model_kept_prefix * 7 + value;
                    ++kept_so_far;
                }
            }
            if (position >= skipped) model_skipped += value;
            model_dot += value * element_at(other, position);
            is_wanted_found = is_wanted_found || value == wanted;
            is_each_below = is_each_below && value < wanted;
        }

        assert(elements.walk()
                   .map([](std::uint64_t const& value) noexcept { return value * value; })
                   .fold(std::uint64_t{0},
                         [](std::uint64_t acc, std::uint64_t square) noexcept { return acc * 31 + square; })
               == model_squares);
        assert(elements.walk().filter(is_kept).count() == model_kept);
        assert(elements.walk()
                   .filter(is_kept)
                   .limit(ElementCount{limit})
                   .fold(std::uint64_t{0},
                         [](std::uint64_t acc, std::uint64_t const& value) noexcept { return acc * 7 + value; })
               == model_kept_prefix);
        assert(elements.walk()
                   .skip(ElementCount{skipped})
                   .fold(std::uint64_t{0},
                         [](std::uint64_t acc, std::uint64_t const& value) noexcept { return acc + value; })
               == model_skipped);
        assert(elements.walk().limit(ElementCount{limit}).size() == (limit < size ? limit : size));
        assert(
            elements.walk()
                .try_zip(others.walk())
                .expect("the two windows hold size elements")
                .map([](auto const& pair) noexcept {
                    auto const& [first, second] = pair;
                    return *first * *second;
                })
                .fold(std::uint64_t{0}, [](std::uint64_t acc, std::uint64_t product) noexcept { return acc + product; })
            == model_dot);
        assert(elements.walk().any_of([wanted](std::uint64_t const& value) noexcept { return value == wanted; })
               == is_wanted_found);
        assert(elements.walk().all_of([wanted](std::uint64_t const& value) noexcept { return value < wanted; })
               == is_each_below);

        std::size_t cursor_kept = 0;
        for (std::uint64_t const value : elements.walk().filter(is_kept)) {
            assert(is_kept(value));
            ++cursor_kept;
        }
        assert(cursor_kept == model_kept);

        // flat_map: each element gives the run of its first value % 8
        // elements of the other run.
        std::size_t model_flat = 0;
        for (std::size_t position = 0; position < size; ++position) {
            model_flat += element_at(run, position) % 8;
        }
        View<std::uint64_t const> const pool = other.view();
        assert(elements.walk()
                   .flat_map([pool](std::uint64_t const& value) noexcept {
                       return pool.window(0, value % 8).expect("a prefix fits").walk();
                   })
                   .count()
               == model_flat);
    }
}

// A cursor of a walk never leaves its loop: a read at the end and a step
// past the end end the process in each build.  Each one runs in a child
// through expect_dies of the Check family.
void test_cursors_at_the_end_abort(::fixy::TestRunnerCtx const& test) {
    using ::fixy::check::expect_dies;
    FixedArray<int, 4> const values{1, 2, 3, 4};
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
    expect_dies(
        test,
        [&values] noexcept {
            auto const kept =
                values.view().walk().filter([](int const&) noexcept { return true; }).limit(ElementCount{2});
            static_cast<void>(*kept.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a limit of a walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const kept =
                values.view().walk().filter([](int const&) noexcept { return true; }).limit(ElementCount{2});
            auto cursor = kept.begin();
            ++cursor;
            ++cursor;
            ++cursor;
        },
        ::fixy::TextView{"foundation: fatal: a step past the end cursor of a limit of a walk"});
    expect_dies(
        test,
        [&values] noexcept {
            auto const doubled = values.view().walk().map([](int const& value) noexcept { return value * 2; });
            static_cast<void>(*doubled.end());
        },
        ::fixy::TextView{"foundation: fatal: a read through the end cursor of a View"});
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_float_fold_runs_left_to_right();
    test_cursor_loop_writes_through_the_walk();
    test_try_zip_compares_the_counts();
    test_chains_agree_with_the_index_model();
    test_cursors_at_the_end_abort(test);
    crucible::test::pass("test_core_walk: all tests passed\n");
    return 0;
}
