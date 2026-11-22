// Tests of foundation/core/Reduce.h: the tree sum against a naive tree of
// the same rule, for each count of lanes and for the counts around each
// power of two; the split into grains in random orders and for 1, 2, 4, 8
// and 64 shards; the error bound; the sum of runs and the segments.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Reduce.h>
#include <foundation/effects/Effect.h>

#include "nan_outputs.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;
using ::foundation::core::with_mut;

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, as probe E4 drew them: the association of a sum changes its bits.
[[nodiscard]] float draw_float(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
    auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
    auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
    return ::fixy::bit_cast<float>((sign << 31) | (exponent << 23) | mantissa);
}

[[nodiscard]] double draw_double(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    std::uint64_t const mantissa = stream.next_wide() & ((std::uint64_t{1} << 52) - 1);
    std::uint64_t const exponent = 1023 + bits % 40 - 20;
    std::uint64_t const sign = (bits >> 40) & 1u;
    return ::fixy::bit_cast<double>((sign << 63) | (exponent << 52) | mantissa);
}

[[nodiscard]] HeapArray<float> draw_floats(std::size_t count, std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    return ::foundation::core::mint_heap_array_with<float>(
        test_alloc(), count, [&stream](std::size_t) noexcept { return draw_float(stream); });
}

[[nodiscard]] HeapArray<double> draw_doubles(std::size_t count, std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    return ::foundation::core::mint_heap_array_with<double>(
        test_alloc(), count, [&stream](std::size_t) noexcept { return draw_double(stream); });
}

[[nodiscard]] bool same_bits(float left, float right) noexcept {
    return ::fixy::bit_cast<std::uint32_t>(left) == ::fixy::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool same_bits(double left, double right) noexcept {
    return ::fixy::bit_cast<std::uint64_t>(left) == ::fixy::bit_cast<std::uint64_t>(right);
}

// The naive tree of the rule: split at the largest power of two below the
// count, the left part first.  It finds the split with a loop and not with
// the bit operations of the header.
template <class T>
[[nodiscard]] T naive_tree(View<T const> run) noexcept {
    if (run.size() == 1) return *run.get(0).expect("a test reads an element at or past the count of its View");
    std::size_t split = 1;
    while (split * 2 < run.size()) {
        split *= 2;
    }
    T const left = naive_tree(run.window(0, split).expect("the left part fits"));
    T const right = naive_tree(run.window(split, run.size() - split).expect("the right part fits"));
    return left + right;
}

// The left fold, the order that the tree replaces.
[[nodiscard]] float left_fold(View<float const> run) noexcept {
    float sum = 0.0f;
    for (float const value : run) {
        sum += value;
    }
    return sum;
}

// The counts of a property test: each count to 600, and the counts around
// each power of two from 2^10 to the limit.
template <class Body>
void for_each_count(std::size_t limit, Body body) noexcept {
    for (std::size_t count = 0; count <= 600 && count <= limit; ++count) {
        body(count);
    }
    for (std::size_t power = std::size_t{1} << 10; power <= limit; power *= 2) {
        for (std::size_t count = power - 17; count <= power + 17 && count <= limit; ++count) {
            body(count);
        }
    }
}

// sum_by_tree, reduce_by_tree with an add, each count of lanes and the naive tree
// give the same bits for each count.
void test_sum_by_tree_agrees_with_the_naive_tree() {
    HeapArray<float> const run = draw_floats(std::size_t{1} << 16, 0x7E570001u);
    View<float const> const all{run.view()};
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    for_each_count(all.size(), [&](std::size_t count) noexcept {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        float const sum = ::foundation::core::sum_by_tree(part);
        if (count == 0) {
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, 0.0f));
            CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::reduce_by_tree(part, add).is_none());
            return;
        }
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, naive_tree(part)));
        CRUCIBLE_TEST_ASSERT(
            kTest, same_bits(sum, ::foundation::core::reduce_by_tree(part, add).expect("the part has elements")));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<2>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<4>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<8>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<16>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<32>(part)));
    });
}

void test_sum_by_tree_of_doubles_agrees_with_the_naive_tree() {
    HeapArray<double> const run = draw_doubles(std::size_t{1} << 13, 0x7E570002u);
    View<double const> const all{run.view()};
    for_each_count(all.size(), [&](std::size_t count) noexcept {
        View<double const> const part = all.window(0, count).expect("the part fits the run");
        double const sum = ::foundation::core::sum_by_tree(part);
        if (count == 0) {
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, 0.0));
            return;
        }
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, naive_tree(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<2>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<4>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<8>(part)));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, ::foundation::core::sum_by_tree_in_lanes<16>(part)));
    });
}

// The positive control of the bit tests: on the data of probe E4, a left
// fold of 2^20 floats gives bits that differ from the tree.  So the data can
// show a change of the order.
void test_left_fold_differs_from_the_tree() {
    HeapArray<float> const run = draw_floats(std::size_t{1} << 20, 0x7E570003u);
    View<float const> const all{run.view()};
    CRUCIBLE_TEST_ASSERT(kTest, !same_bits(left_fold(all), ::foundation::core::sum_by_tree(all)));
}

// The positions 0 to count - 1 in an order that the stream draws.
[[nodiscard]] HeapArray<std::size_t> draw_order(std::size_t count, ::crucible::test::PhiloxStream& stream) noexcept {
    HeapArray<std::size_t> order = ::foundation::core::mint_heap_array(test_alloc(), count, std::size_t{0});
    order = with_mut(std::move(order), [&stream, count](auto slots) noexcept {
        for (std::size_t index = 0; index < count; ++index) {
            *slots.get(index).expect("a test reads an element at or past the count of its View") = index;
        }
        for (std::size_t index = count; index > 1; --index) {
            std::size_t const other = stream.below(index);
            std::size_t const kept =
                *slots.get(index - 1).expect("a test reads an element at or past the count of its View");
            *slots.get(index - 1).expect("a test reads an element at or past the count of its View") =
                *slots.get(other).expect("a test reads an element at or past the count of its View");
            *slots.get(other).expect("a test reads an element at or past the count of its View") = kept;
        }
    });
    return order;
}

// The start of a shard of a proportional split, as mint_split cuts a region.
[[nodiscard]] std::size_t shard_start(std::size_t total, std::size_t shards, std::size_t shard) noexcept {
    std::size_t const smaller = total / shards;
    std::size_t const larger = total % shards;
    return shard * smaller + (shard < larger ? shard : larger);
}

// The partials of a split into grains of Grain, computed by shards shards in
// an order that the stream draws.  Each shard takes a proportional cut of the
// grains, as a parallel loop does, and computes its grains in a drawn order.
// Each grain writes its partial through a frame of its own.
template <std::size_t Grain>
[[nodiscard]] float sum_by_shards(View<float const> run, std::size_t shards,
                                  ::crucible::test::PhiloxStream& stream) noexcept {
    std::size_t const grains = ::foundation::core::count_tree_grains<Grain>(run.size());
    HeapArray<float> partials = ::foundation::core::mint_heap_array(test_alloc(), grains, 0.0f);
    HeapArray<std::size_t> const shard_order = draw_order(shards, stream);
    partials = with_mut(std::move(partials), [&](auto slots) noexcept {
        for (std::size_t const shard : shard_order) {
            std::size_t const first = shard_start(grains, shards, shard);
            std::size_t const count = shard_start(grains, shards, shard + 1) - first;
            HeapArray<std::size_t> const grain_order = draw_order(count, stream);
            for (std::size_t const offset : grain_order) {
                ::fixy::FixedArray<float, 1> one{};
                one.with_mut([run, first, offset](auto own) noexcept {
                    ::foundation::core::sum_grains_by_tree<Grain>(run, first + offset, std::move(own));
                });
                *slots.get(first + offset).expect("a test reads an element at or past the count of its View") =
                    *one.get(0).expect("a test reads an element at or past the count of its array");
            }
        }
    });
    return ::foundation::core::sum_by_tree(View<float const>{partials.view()});
}

// Each grain, each shard count and each drawn order give the bits of the
// tree sum of the run.
void test_grains_split_the_tree_at_its_nodes() {
    HeapArray<float> const run = draw_floats(std::size_t{1} << 17, 0x7E570004u);
    View<float const> const all{run.view()};
    ::crucible::test::PhiloxStream stream{0x7E570005u};
    ::fixy::FixedArray<std::size_t, 17> const counts{1,    2,    3,    15,    16,    17,    255,    256,   257,
                                                     4095, 4096, 4097, 65535, 65536, 65537, 131071, 131072};
    ::fixy::FixedArray<std::size_t, 5> const shard_counts{1, 2, 4, 8, 64};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        float const sum = ::foundation::core::sum_by_tree(part);
        for (std::size_t const shards : shard_counts) {
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, sum_by_shards<1>(part, shards, stream)));
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, sum_by_shards<16>(part, shards, stream)));
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, sum_by_shards<256>(part, shards, stream)));
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(sum, sum_by_shards<4096>(part, shards, stream)));
        }
    }
}

// The generic grains with an order-sensitive combine: the tree of the
// partials is the tree of the run.
void test_generic_grains_keep_the_order_of_operands() {
    ::crucible::test::PhiloxStream stream{0x7E570006u};
    HeapArray<std::uint64_t> const run = ::foundation::core::mint_heap_array_with<std::uint64_t>(
        test_alloc(), std::size_t{5000}, [&stream](std::size_t) noexcept { return stream.next_wide(); });
    auto const mix = [](std::uint64_t const& left, std::uint64_t const& right) noexcept {
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    };
    View<std::uint64_t const> const all{run.view()};
    ::fixy::FixedArray<std::size_t, 5> const counts{1, 63, 64, 65, 5000};
    for (std::size_t const count : counts) {
        View<std::uint64_t const> const part = all.window(0, count).expect("the part fits the run");
        std::uint64_t const whole = ::foundation::core::reduce_by_tree(part, mix).expect("the part has elements");
        std::size_t const grains = ::foundation::core::count_tree_grains<64>(count);
        HeapArray<std::uint64_t> partials = ::foundation::core::mint_heap_array(test_alloc(), grains, std::uint64_t{0});
        partials = with_mut(std::move(partials), [&](auto out) noexcept {
            ::foundation::core::reduce_grains_by_tree<64>(part, 0, std::move(out), mix);
        });
        std::uint64_t const split =
            ::foundation::core::reduce_by_tree(View<std::uint64_t const>{partials.view()}, mix).expect("one grain");
        CRUCIBLE_TEST_ASSERT(kTest, whole == split);
    }
}

// The error of the float tree sum stays inside gamma(d) * sum |x_i|.  The
// double tree sum of the same floats is the model of the exact sum: its own
// error is below 2^-29 of the bound of the float sum.
void test_error_stays_inside_the_bound() {
    for_each_count(std::size_t{1} << 16, [](std::size_t count) noexcept {
        if (count == 0) return;
        HeapArray<float> const run = draw_floats(count, 0x7E570007u + count);
        HeapArray<double> wide = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0);
        double absolute_sum = 0.0;
        wide = with_mut(std::move(wide), [&run, &absolute_sum](auto wide_view) noexcept {
            std::size_t position = 0;
            for (float const value : run) {
                *wide_view.get(position).expect("a test reads an element at or past the count of its View") =
                    static_cast<double>(value);
                absolute_sum += value < 0.0f ? -static_cast<double>(value) : static_cast<double>(value);
                ++position;
            }
        });
        double const exact = ::foundation::core::sum_by_tree(View<double const>{wide.view()});
        double const computed = static_cast<double>(::foundation::core::sum_by_tree(View<float const>{run.view()}));
        double const depth = ::foundation::core::count_tree_depth(count);
        double const unit = 1.0 / 16777216.0;
        double const gamma = depth * unit / (1.0 - depth * unit);
        double const error = computed > exact ? computed - exact : exact - computed;
        CRUCIBLE_TEST_ASSERT(kTest, error <= gamma * absolute_sum * (1.0 + 1.0e-6));
    });
}

// The sum of runs is the tree of the runs at each element, in run order.
void test_sum_of_runs_takes_the_tree_of_the_runs() {
    constexpr std::size_t kRuns = 7;
    constexpr std::size_t kCount = 517;
    HeapArray<float> const first = draw_floats(kCount, 0x7E570010u);
    HeapArray<float> const second = draw_floats(kCount, 0x7E570011u);
    HeapArray<float> const third = draw_floats(kCount, 0x7E570012u);
    HeapArray<float> const fourth = draw_floats(kCount, 0x7E570013u);
    HeapArray<float> const fifth = draw_floats(kCount, 0x7E570014u);
    HeapArray<float> const sixth = draw_floats(kCount, 0x7E570015u);
    HeapArray<float> const seventh = draw_floats(kCount, 0x7E570016u);
    ::fixy::FixedArray<View<float const>, kRuns> const runs{
        View<float const>{first.view()},  View<float const>{second.view()}, View<float const>{third.view()},
        View<float const>{fourth.view()}, View<float const>{fifth.view()},  View<float const>{sixth.view()},
        View<float const>{seventh.view()}};
    HeapArray<float> sums = ::foundation::core::mint_heap_array(test_alloc(), kCount, 0.0f);
    ::fixy::FixedArray<std::size_t, 4> const run_counts{1, 2, 3, 7};
    for (std::size_t const used : run_counts) {
        View<View<float const> const> const some_runs =
            View<View<float const> const>{runs.view()}.window(0, used).expect("the runs fit");
        sums = with_mut(std::move(sums), [&](auto out) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::sum_runs_by_tree(some_runs, std::move(out)).is_ok());
        });
        HeapArray<float> column = ::foundation::core::mint_heap_array(test_alloc(), used, 0.0f);
        column = with_mut(std::move(column), [&sums, some_runs, used](auto cells) noexcept {
            View<float> const column_view = cells.view();
            for (std::size_t offset = 0; offset < kCount; ++offset) {
                for (std::size_t run = 0; run < used; ++run) {
                    *column_view.get(run).expect("a test reads an element at or past the count of its View") =
                        *some_runs.get(run)
                             .expect("a test reads an element at or past the count of its View")
                             ->get(offset)
                             .expect("a test reads an element at or past the count of its View");
                }
                CRUCIBLE_TEST_ASSERT(kTest, same_bits(*sums.view().get(offset).expect(
                                                          "a test reads an element at or past the count of its View"),
                                                      naive_tree(View<float const>{column_view})));
            }
        });
    }
    HeapArray<float> const short_run = draw_floats(kCount - 1, 0x7E570017u);
    ::fixy::FixedArray<View<float const>, 2> const uneven{View<float const>{first.view()},
                                                          View<float const>{short_run.view()}};
    sums = with_mut(std::move(sums), [&](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(
            kTest, ::foundation::core::sum_runs_by_tree(View<View<float const> const>{uneven.view()}, std::move(out))
                       .is_err());
    });
}

// Each segment takes the tree sum of its window, and an empty one gives +0.
void test_segments_take_the_tree_of_their_window() {
    HeapArray<float> const run = draw_floats(5000, 0x7E570020u);
    View<float const> const all{run.view()};
    ::fixy::FixedArray<std::size_t, 7> const offsets{3, 3, 20, 1044, 1045, 4096, 5000};
    HeapArray<float> sums = ::foundation::core::mint_heap_array(test_alloc(), std::size_t{6}, 1.0f);
    sums = with_mut(std::move(sums), [&](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::sum_segments_by_tree(
                                        all, View<std::size_t const>{offsets.view()}, std::move(out))
                                        .is_ok());
    });
    for (std::size_t segment = 0; segment < 6; ++segment) {
        std::size_t const start =
            *offsets.get(segment).expect("a test reads an element at or past the count of its array");
        std::size_t const end =
            *offsets.get(segment + 1).expect("a test reads an element at or past the count of its array");
        float const expected =
            ::foundation::core::sum_by_tree(all.window(start, end - start).expect("the segment fits the run"));
        CRUCIBLE_TEST_ASSERT(kTest, same_bits(*sums.view().get(segment).expect(
                                                  "a test reads an element at or past the count of its View"),
                                              expected));
    }
    CRUCIBLE_TEST_ASSERT(
        kTest, same_bits(*sums.view().get(0).expect("a test reads an element at or past the count of its View"), 0.0f));
    ::fixy::FixedArray<std::size_t, 3> const past_end{0, 10, 5001};
    sums = with_mut(std::move(sums), [&](auto out) noexcept {
        auto [two, rest] = ::foundation::core::split_at(std::move(out), 2).expect("two sums fit");
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::sum_segments_by_tree(
                                        all, View<std::size_t const>{past_end.view()}, std::move(two))
                                        .is_err());
        static_cast<void>(rest.size());
    });
}

// True when a sum of runs gave the error kind.
[[nodiscard]] bool
is_refused_with(::foundation::core::Result<::foundation::core::Unit, ::foundation::core::RunsError> const& result,
                ::foundation::core::RunsError kind) noexcept {
    return result.match([](::foundation::core::Unit const&) noexcept { return false; },
                        [kind](::foundation::core::RunsError const& error) noexcept { return error == kind; });
}

// The sum of runs into the count sums from offset of buffer, through a frame
// of the buffer and two splits of its ViewMut.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::RunsError>
sum_runs_into(HeapArray<float>& buffer, View<View<float const> const> runs, std::size_t offset,
              std::size_t count) noexcept {
    auto [result, back] = with_mut(std::move(buffer), [&](auto whole) noexcept {
        auto [low, rest] = ::foundation::core::split_at(std::move(whole), offset).expect("the offset fits");
        auto [window, high] = ::foundation::core::split_at(std::move(rest), count).expect("the window fits");
        static_cast<void>(low.size() + high.size());
        return ::foundation::core::sum_runs_by_tree(runs, std::move(window));
    });
    buffer = std::move(back);
    return result;
}

// The sums can be the View of one run: each vector of sums is written after
// the reads of each run at its positions.  A View of sums that shares some
// elements with a run, and does not start where the run starts, is refused
// with no write, in each direction of the shift.
void test_sum_of_runs_refuses_a_partial_overlap() {
    constexpr std::size_t kCount = 300;
    HeapArray<float> buffer = draw_floats(kCount + 2, 0x7E570040u);
    HeapArray<float> const other = draw_floats(kCount, 0x7E570041u);
    HeapArray<float> before = ::foundation::core::mint_heap_array(test_alloc(), kCount + 2, 0.0f);
    before = with_mut(std::move(before), [&buffer](auto cells) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::copy(cells.view(), View<float const>{buffer.view()}).is_ok());
    });
    View<float const> const run_view = buffer.view().window(1, kCount).expect("the run fits the buffer");
    ::fixy::FixedArray<View<float const>, 2> const runs{View<float const>{run_view}, View<float const>{other.view()}};
    View<View<float const> const> const both{runs.view()};
    using ::foundation::core::RunsError;
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_with(sum_runs_into(buffer, both, 2, kCount), RunsError::PartialOverlap));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused_with(sum_runs_into(buffer, both, 0, kCount), RunsError::PartialOverlap));
    for (std::size_t position = 0; position < kCount + 2; ++position) {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            same_bits(*buffer.view().get(position).expect("a test reads an element at or past the count of its View"),
                      *before.view().get(position).expect("a test reads an element at or past the count of its View")));
    }
    CRUCIBLE_TEST_ASSERT(kTest, sum_runs_into(buffer, both, 1, kCount).is_ok());
    for (std::size_t offset = 0; offset < kCount; ++offset) {
        float const expected =
            *before.view().get(1 + offset).expect("a test reads an element at or past the count of its View")
            + *other.view().get(offset).expect("a test reads an element at or past the count of its View");
        CRUCIBLE_TEST_ASSERT(
            kTest, same_bits(*run_view.get(offset).expect("a test reads an element at or past the count of its View"),
                             expected));
    }
}

// ── The canonical NaN ───────────────────────────────────────────────────

using ::crucible::test::float_bits;
using ::crucible::test::output_bits;

// Each form of the tree sum of part gives the output bits of the naive tree.
template <class T>
void check_sum_outputs(View<T const> part) noexcept {
    auto const expected = output_bits(naive_tree(part));
    CRUCIBLE_TEST_ASSERT(kTest, float_bits(::foundation::core::sum_by_tree(part)) == expected);
    CRUCIBLE_TEST_ASSERT(kTest, float_bits(::foundation::core::sum_by_tree_in_lanes<2>(part)) == expected);
    CRUCIBLE_TEST_ASSERT(kTest, float_bits(::foundation::core::sum_by_tree_in_lanes<4>(part)) == expected);
    CRUCIBLE_TEST_ASSERT(kTest, float_bits(::foundation::core::sum_by_tree_in_lanes<8>(part)) == expected);
    CRUCIBLE_TEST_ASSERT(kTest, float_bits(::foundation::core::sum_by_tree_in_lanes<16>(part)) == expected);
}

// The short runs whose sum is a NaN with other bits on x86 without the rule:
// +inf plus -inf, which gives 0xFFC00000, a NaN with a payload, a signaling
// NaN alone, and a NaN with its sign bit set.  An infinity that is not a NaN
// keeps its bits.
template <class T, class Word>
void check_short_runs(Word infinity, Word sign, Word quiet) noexcept {
    ::fixy::FixedArray<T, 2> const infinities{::fixy::bit_cast<T>(infinity), ::fixy::bit_cast<T>(sign | infinity)};
    ::fixy::FixedArray<T, 2> const payload{::fixy::bit_cast<T>(quiet | Word{0x12345}), T{1}};
    ::fixy::FixedArray<T, 1> const signaling{::fixy::bit_cast<T>(infinity | Word{1})};
    ::fixy::FixedArray<T, 2> const negative{::fixy::bit_cast<T>(sign | quiet), T{2}};
    ::fixy::FixedArray<T, 2> const infinite{::fixy::bit_cast<T>(infinity), T{1}};
    check_sum_outputs(View<T const>{infinities.view()});
    check_sum_outputs(View<T const>{payload.view()});
    check_sum_outputs(View<T const>{signaling.view()});
    check_sum_outputs(View<T const>{negative.view()});
    check_sum_outputs(View<T const>{infinite.view()});
    CRUCIBLE_TEST_ASSERT(kTest,
                         float_bits(::foundation::core::sum_by_tree(View<T const>{infinite.view()})) == infinity);
}

// Each output of a sum form that is a NaN is the canonical NaN, and each
// other output keeps the bits of its adds: the tree sums for each count of
// lanes, the segments, the grains and the sum of runs.
void test_nan_outputs_are_canonical() {
    check_short_runs<float>(std::uint32_t{0x7F800000u}, std::uint32_t{0x80000000u}, std::uint32_t{0x7FC00000u});
    check_short_runs<double>(std::uint64_t{0x7FF0000000000000u}, std::uint64_t{0x8000000000000000u},
                             std::uint64_t{0x7FF8000000000000u});
    CRUCIBLE_TEST_ASSERT(kTest,
                         float_bits(::foundation::core::quiet_nan_of<float>) == ::crucible::test::kCanonicalFloatNan);
    HeapArray<float> run = draw_floats(4099, 0x7E570030u);
    run = with_mut(std::move(run),
                   [](auto cells) noexcept { ::crucible::test::put_specials(cells.view(), 0x7E570031u); });
    HeapArray<double> wide = draw_doubles(1000, 0x7E570032u);
    wide = with_mut(std::move(wide),
                    [](auto cells) noexcept { ::crucible::test::put_specials(cells.view(), 0x7E570033u); });
    View<float const> const all{run.view()};
    for_each_count(all.size(), [&](std::size_t count) noexcept {
        if (count == 0) return;
        check_sum_outputs(all.window(0, count).expect("the part fits the run"));
    });
    View<double const> const all_wide{wide.view()};
    for (std::size_t count = 1; count <= all_wide.size(); count += 7) {
        check_sum_outputs(all_wide.window(0, count).expect("the part fits the run"));
    }
    ::fixy::FixedArray<std::size_t, 8> const offsets{0, 1, 2, 17, 64, 300, 1100, 4099};
    HeapArray<float> sums = ::foundation::core::mint_heap_array(test_alloc(), std::size_t{7}, 0.0f);
    sums = with_mut(std::move(sums), [&](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::sum_segments_by_tree(
                                        all, View<std::size_t const>{offsets.view()}, std::move(out))
                                        .is_ok());
    });
    for (std::size_t segment = 0; segment < 7; ++segment) {
        std::size_t const start =
            *offsets.get(segment).expect("a test reads an element at or past the count of its array");
        std::size_t const end =
            *offsets.get(segment + 1).expect("a test reads an element at or past the count of its array");
        float const expected = naive_tree(all.window(start, end - start).expect("the segment fits the run"));
        CRUCIBLE_TEST_ASSERT(kTest, float_bits(*sums.view().get(segment).expect(
                                        "a test reads an element at or past the count of its View"))
                                        == output_bits(expected));
    }
    std::size_t const grains = ::foundation::core::count_tree_grains<16>(all.size());
    HeapArray<float> partials = ::foundation::core::mint_heap_array(test_alloc(), grains, 0.0f);
    partials = with_mut(std::move(partials),
                        [&](auto out) noexcept { ::foundation::core::sum_grains_by_tree<16>(all, 0, std::move(out)); });
    for (std::size_t grain = 0; grain < grains; ++grain) {
        std::size_t const start = grain * 16;
        std::size_t const size = all.size() - start < 16 ? all.size() - start : 16;
        float const expected = naive_tree(all.window(start, size).expect("the grain fits the run"));
        CRUCIBLE_TEST_ASSERT(kTest, float_bits(*partials.view().get(grain).expect(
                                        "a test reads an element at or past the count of its View"))
                                        == output_bits(expected));
    }
    ::fixy::FixedArray<View<float const>, 3> const runs{all.window(0, 1000).expect("a run fits"),
                                                        all.window(1000, 1000).expect("a run fits"),
                                                        all.window(2000, 1000).expect("a run fits")};
    HeapArray<float> run_sums = ::foundation::core::mint_heap_array(test_alloc(), std::size_t{1000}, 0.0f);
    run_sums = with_mut(std::move(run_sums), [&](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            ::foundation::core::sum_runs_by_tree(View<View<float const> const>{runs.view()}, std::move(out)).is_ok());
    });
    for (std::size_t offset = 0; offset < 1000; ++offset) {
        float const pair = *all.get(offset).expect("a test reads an element at or past the count of its View")
                         + *all.get(1000 + offset).expect("a test reads an element at or past the count of its View");
        float const expected =
            pair + *all.get(2000 + offset).expect("a test reads an element at or past the count of its View");
        CRUCIBLE_TEST_ASSERT(kTest, float_bits(*run_sums.view().get(offset).expect(
                                        "a test reads an element at or past the count of its View"))
                                        == output_bits(expected));
    }
}

}  // namespace

int main() {
    test_sum_by_tree_agrees_with_the_naive_tree();
    test_sum_by_tree_of_doubles_agrees_with_the_naive_tree();
    test_left_fold_differs_from_the_tree();
    test_grains_split_the_tree_at_its_nodes();
    test_generic_grains_keep_the_order_of_operands();
    test_error_stays_inside_the_bound();
    test_sum_of_runs_takes_the_tree_of_the_runs();
    test_segments_take_the_tree_of_their_window();
    test_nan_outputs_are_canonical();
    test_sum_of_runs_refuses_a_partial_overlap();
    ::fixy::check::pass(kTest, "test_core_reduce: all tests passed\n");
    return 0;
}
