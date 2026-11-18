// Tests of foundation/core/Scan.h: the sum scans against a naive fold of the
// blocks of the stated order, the scan in place, the split into grains for
// 1, 2, 4, 8 and 64 shards in drawn orders, the error bound, and the plan of
// larger scans.

#include <foundation/core/Scan.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"
#include "nan_outputs.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

[[nodiscard]] float draw_float(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
    auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
    auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
    return ::fixy::bit_cast<float>((sign << 31) | (exponent << 23) | mantissa);
}

[[nodiscard]] HeapArray<float> draw_floats(std::size_t count, std::uint64_t seed) noexcept {
    HeapArray<float> run = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
    ::crucible::test::PhiloxStream stream{seed};
    for (float& value : run) {
        value = draw_float(stream);
    }
    return run;
}

[[nodiscard]] HeapArray<float> zeros(std::size_t count) noexcept {
    return ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
}

[[nodiscard]] bool same_bits(float left, float right) noexcept {
    return ::fixy::bit_cast<std::uint32_t>(left) == ::fixy::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool same_runs(View<float const> left, View<float const> right) noexcept {
    if (left.size() != right.size()) return false;
    for (auto const [first, second] : left.walk().try_zip(right.walk()).expect("the two runs have one count")) {
        if (!same_bits(*first, *second)) return false;
    }
    return true;
}

// Runs scan on the ViewMut of the count first elements of owner, through a
// frame of the owner and a split of its ViewMut.  True when the Result of the
// scan holds a value.
template <class Owner, class Scan>
[[nodiscard]] bool scan_into(Owner& owner, std::size_t count, Scan scan) noexcept {
    auto [is_ok, back] = ::foundation::core::with_mut(std::move(owner), [&](auto whole) noexcept {
        auto [part, rest] = ::foundation::core::split_at(std::move(whole), count).expect("the part fits the owner");
        static_cast<void>(rest.size());
        return scan(std::move(part)).is_ok();
    });
    owner = std::move(back);
    return is_ok;
}

// Runs write on the ViewMut of the count elements from offset of owner,
// through a frame of the owner and two splits of its ViewMut.
template <class Owner, class Write>
void write_window(Owner& owner, std::size_t offset, std::size_t count, Write write) noexcept {
    owner = ::foundation::core::with_mut(std::move(owner), [&](auto whole) noexcept {
        auto [low, rest] = ::foundation::core::split_at(std::move(whole), offset).expect("the offset fits the owner");
        auto [window, high] = ::foundation::core::split_at(std::move(rest), count).expect("the window fits the owner");
        static_cast<void>(low.size() + high.size());
        write(std::move(window));
    });
}

// The naive form of THE ORDER: the blocks of k from the largest, each a tree
// sum, folded from the left.  The prefix of no element is +0.
[[nodiscard]] float naive_prefix(View<float const> run, std::size_t position) noexcept {
    float fold = 0.0f;
    bool has_block = false;
    std::size_t start = 0;
    for (std::size_t block = std::size_t{1} << 62; block > 0; block /= 2) {
        if ((position & block) == 0) continue;
        float const sum = ::foundation::core::sum_by_tree(run.window(start, block).expect("the block fits the run"));
        fold = has_block ? fold + sum : sum;
        has_block = true;
        start += block;
    }
    return fold;
}

// Each output of the two sum scans is the naive fold of THE ORDER, for each
// count to 300 and for the counts around 1024, 2048 and 4096.
void test_sum_scans_agree_with_the_naive_fold() {
    HeapArray<float> const run = draw_floats(4200, 0x5CA40001u);
    View<float const> const all{run.view()};
    HeapArray<float> inclusive = zeros(4200);
    HeapArray<float> exclusive = zeros(4200);
    ::fixy::FixedArray<std::size_t, 6> const long_counts{1023, 1025, 2047, 2049, 4095, 4097};
    auto const check_count = [&](std::size_t count) noexcept {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        View<float const> const inclusive_part =
            View<float const>{inclusive.view()}.window(0, count).expect("the part fits");
        View<float const> const exclusive_part =
            View<float const>{exclusive.view()}.window(0, count).expect("the part fits");
        assert(scan_into(inclusive, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums(part, std::move(out));
        }));
        assert(scan_into(exclusive, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums(part, std::move(out));
        }));
        for (std::size_t position = 0; position < count; ++position) {
            float const prefix = naive_prefix(part, position);
            float const value = ::crucible::test::element_at(part, position);
            float const inclusive_value = position == 0 ? value : prefix + value;
            assert(same_bits(::crucible::test::element_at(exclusive_part, position), prefix));
            assert(same_bits(::crucible::test::element_at(inclusive_part, position), inclusive_value));
        }
    };
    for (std::size_t count = 0; count <= 300; ++count) {
        check_count(count);
    }
    for (std::size_t const count : long_counts) {
        check_count(count);
    }
}

// The sum scans with 2, 4, 8, 16 and 32 lanes give the bits of the generic
// recursion with an add, for each count to 600 and the counts around each
// power of two to 2^14.
void test_sum_scan_lanes_agree_with_the_recursion() {
    HeapArray<float> const run = draw_floats((std::size_t{1} << 14) + 17, 0x5CA40007u);
    View<float const> const all{run.view()};
    HeapArray<float> expected = zeros(run.size());
    HeapArray<float> got = zeros(run.size());
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    auto const check_count = [&](std::size_t count) noexcept {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        View<float const> const want_view = View<float const>{expected.view()}.window(0, count).expect("the part fits");
        View<float const> const out_view = View<float const>{got.view()}.window(0, count).expect("the part fits");
        assert(scan_into(expected, count, [&](auto want) noexcept {
            return ::foundation::core::scan_inclusive(part, std::move(want), add);
        }));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<2>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<4>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<8>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<16>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<32>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(expected, count, [&](auto want) noexcept {
            return ::foundation::core::scan_exclusive(part, std::move(want), 0.0f, add);
        }));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums_in_lanes<2>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums_in_lanes<16>(part, std::move(out));
        }));
        assert(same_runs(want_view, out_view));
    };
    for (std::size_t count = 0; count <= 600; ++count) {
        check_count(count);
    }
    for (std::size_t power = 1024; power <= std::size_t{1} << 14; power *= 2) {
        for (std::size_t count = power - 17; count <= power + 17; ++count) {
            check_count(count);
        }
    }
}

// The kinds of a run of zeros: each element +0, each element -0, the two in
// turn, and pairs of a value and its negation, which cancel to +0.
enum class ZeroRun : std::uint8_t {
    Positive = 0,
    Negative = 1,
    Alternating = 2,
    Cancelling = 3,
};

[[nodiscard]] HeapArray<float> zero_run(ZeroRun kind, std::size_t count, std::uint64_t seed) noexcept {
    HeapArray<float> run = zeros(count);
    ::crucible::test::PhiloxStream stream{seed};
    float last = 0.0f;
    std::size_t position = 0;
    for (float& value : run) {
        switch (kind) {
            case ZeroRun::Positive:
                value = 0.0f;
                break;
            case ZeroRun::Negative:
                value = -0.0f;
                break;
            case ZeroRun::Alternating:
                value = position % 2 == 0 ? 0.0f : -0.0f;
                break;
            default:
                last = position % 2 == 0 ? draw_float(stream) : -last;
                value = last;
                break;
        }
        ++position;
    }
    return run;
}

// The -0 that a sum scan in lanes adds for an absent term keeps the bits of
// each zero: each count of lanes gives the bits of the recursion on runs of
// zeros of each sign and on pairs that cancel.
void test_sum_scan_lanes_keep_the_sign_of_zero() {
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    ::fixy::FixedArray<ZeroRun, 4> const kinds{ZeroRun::Positive, ZeroRun::Negative, ZeroRun::Alternating,
                                               ZeroRun::Cancelling};
    ::fixy::FixedArray<std::size_t, 8> const counts{0, 1, 2, 17, 100, 1023, 1024, 1025};
    for (ZeroRun const kind : kinds) {
        for (std::size_t const count : counts) {
            HeapArray<float> const run = zero_run(kind, count, 0x5CA40008u + count);
            View<float const> const in{run.view()};
            HeapArray<float> want = zeros(count);
            HeapArray<float> got = zeros(count);
            View<float const> const want_view{want.view()};
            View<float const> const got_view{got.view()};
            assert(scan_into(want, count, [&](auto out) noexcept {
                return ::foundation::core::scan_inclusive(in, std::move(out), add);
            }));
            assert(scan_into(got, count, [&](auto out) noexcept {
                return ::foundation::core::scan_inclusive_sums_in_lanes<4>(in, std::move(out));
            }));
            assert(same_runs(want_view, got_view));
            assert(scan_into(got, count, [&](auto out) noexcept {
                return ::foundation::core::scan_inclusive_sums_in_lanes<16>(in, std::move(out));
            }));
            assert(same_runs(want_view, got_view));
            assert(scan_into(want, count, [&](auto out) noexcept {
                return ::foundation::core::scan_exclusive(in, std::move(out), 0.0f, add);
            }));
            assert(scan_into(got, count, [&](auto out) noexcept {
                return ::foundation::core::scan_exclusive_sums_in_lanes<8>(in, std::move(out));
            }));
            assert(same_runs(want_view, got_view));
            assert(scan_into(got, count, [&](auto out) noexcept {
                return ::foundation::core::scan_exclusive_sums(in, std::move(out));
            }));
            assert(same_runs(want_view, got_view));
            if (count == 0) continue;
            float const tree = ::foundation::core::reduce_by_tree(in, add).expect("the run has elements");
            assert(same_bits(::foundation::core::sum_by_tree(in), tree));
            assert(same_bits(::foundation::core::sum_by_tree_in_lanes<16>(in), tree));
        }
    }
}

// A scan in place gives the bits of a scan into a second View, and a scan of
// a longer run gives the same bits at each position of the shorter one.
void test_scan_in_place_and_under_append() {
    HeapArray<float> const run = draw_floats(3000, 0x5CA40002u);
    View<float const> const all{run.view()};
    HeapArray<float> out = zeros(3000);
    assert(scan_into(
        out, 3000, [&](auto part) noexcept { return ::foundation::core::scan_inclusive_sums(all, std::move(part)); }));
    HeapArray<float> in_place = ::foundation::core::mint_heap_array_copy<float>(test_alloc(), all);
    in_place = ::foundation::core::with_mut(std::move(in_place), [](auto whole) noexcept {
        View<float const> const in{whole.view()};
        assert(::foundation::core::scan_inclusive_sums(in, std::move(whole)).is_ok());
    });
    assert(same_runs(View<float const>{out.view()}, View<float const>{in_place.view()}));
    HeapArray<float> shorter = zeros(1777);
    assert(scan_into(shorter, 1777, [&](auto part) noexcept {
        return ::foundation::core::scan_inclusive_sums(all.window(0, 1777).expect("the part fits"), std::move(part));
    }));
    assert(same_runs(View<float const>{shorter.view()},
                     View<float const>{out.view()}.window(0, 1777).expect("the part fits")));
}

[[nodiscard]] HeapArray<std::size_t> draw_order(std::size_t count, ::crucible::test::PhiloxStream& stream) noexcept {
    HeapArray<std::size_t> order = ::foundation::core::mint_heap_array(test_alloc(), count, std::size_t{0});
    View<std::size_t> const slots = order.view();
    for (std::size_t index = 0; index < count; ++index) {
        ::crucible::test::element_at(slots, index) = index;
    }
    for (std::size_t index = count; index > 1; --index) {
        std::size_t const other = stream.below(index);
        std::size_t const kept = ::crucible::test::element_at(slots, index - 1);
        ::crucible::test::element_at(slots, index - 1) = ::crucible::test::element_at(slots, other);
        ::crucible::test::element_at(slots, other) = kept;
    }
    return order;
}

[[nodiscard]] std::size_t shard_start(std::size_t total, std::size_t shards, std::size_t shard) noexcept {
    std::size_t const smaller = total / shards;
    std::size_t const larger = total % shards;
    return shard * smaller + (shard < larger ? shard : larger);
}

// The form of the scan of the grains: the generic form with an add, or the
// sum form with SIMD lanes.
enum class GrainForm : std::uint8_t {
    Generic = 0,
    Sum = 1,
};

// The three phases of a parallel scan, with shards shards in drawn orders:
// the tree sums of the grains, their exclusive scan, and the scan of each
// grain with its carry.
template <std::size_t Grain>
[[nodiscard]] HeapArray<float> scan_by_shards(View<float const> run, ::foundation::core::ScanKind kind,
                                              std::size_t shards, ::crucible::test::PhiloxStream& stream,
                                              GrainForm form = GrainForm::Generic) noexcept {
    std::size_t const grains = ::foundation::core::count_tree_grains<Grain>(run.size());
    HeapArray<float> carries = zeros(grains);
    View<float const> const carry_slots{carries.view()};
    HeapArray<std::size_t> const first_order = draw_order(grains, stream);
    for (std::size_t const grain : first_order) {
        write_window(carries, grain, 1, [&](auto slot) noexcept {
            ::foundation::core::sum_grains_by_tree<Grain>(run, grain, std::move(slot));
        });
    }
    carries = ::foundation::core::with_mut(std::move(carries), [](auto whole) noexcept {
        View<float const> const in{whole.view()};
        assert(::foundation::core::scan_exclusive_sums(in, std::move(whole)).is_ok());
    });
    HeapArray<float> out = zeros(run.size());
    HeapArray<std::size_t> const shard_order = draw_order(shards, stream);
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    for (std::size_t const shard : shard_order) {
        std::size_t const first = shard_start(grains, shards, shard);
        std::size_t const count = shard_start(grains, shards, shard + 1) - first;
        HeapArray<std::size_t> const grain_order = draw_order(count, stream);
        for (std::size_t const offset : grain_order) {
            std::size_t const grain = first + offset;
            View<float const> const carry = carry_slots.window(grain, 1).expect("the carry fits");
            std::size_t const start = grain * Grain;
            std::size_t const size = run.size() - start < Grain ? run.size() - start : Grain;
            bool const is_inclusive = kind == ::foundation::core::ScanKind::Inclusive;
            write_window(out, start, size, [&](auto part) noexcept {
                if (form == GrainForm::Sum && is_inclusive) {
                    ::foundation::core::scan_inclusive_sum_grains<Grain>(run, grain, carry, std::move(part));
                } else if (form == GrainForm::Sum) {
                    ::foundation::core::scan_exclusive_sum_grains<Grain>(run, grain, carry, std::move(part));
                } else if (is_inclusive) {
                    ::foundation::core::scan_inclusive_grains<Grain>(run, grain, carry, std::move(part), add);
                } else {
                    ::foundation::core::scan_exclusive_grains<Grain>(run, grain, carry, std::move(part), 0.0f, add);
                }
            });
        }
    }
    return out;
}

// Each grain, each shard count and each drawn order give the bits of the
// scan of the whole run.
void test_grains_give_the_bits_of_the_scan() {
    HeapArray<float> const run = draw_floats(std::size_t{1} << 15, 0x5CA40003u);
    View<float const> const all{run.view()};
    ::crucible::test::PhiloxStream stream{0x5CA40004u};
    ::fixy::FixedArray<std::size_t, 9> const counts{1, 17, 255, 256, 257, 4097, 20000, 32767, 32768};
    ::fixy::FixedArray<std::size_t, 5> const shard_counts{1, 2, 4, 8, 64};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        HeapArray<float> inclusive = zeros(count);
        HeapArray<float> exclusive = zeros(count);
        assert(scan_into(inclusive, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums(part, std::move(out));
        }));
        assert(scan_into(exclusive, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums(part, std::move(out));
        }));
        View<float const> const whole_inclusive{inclusive.view()};
        View<float const> const whole_exclusive{exclusive.view()};
        for (std::size_t const shards : shard_counts) {
            using ::foundation::core::ScanKind;
            HeapArray<float> const by_single = scan_by_shards<1>(part, ScanKind::Inclusive, shards, stream);
            HeapArray<float> const by_sixteen = scan_by_shards<16>(part, ScanKind::Inclusive, shards, stream);
            HeapArray<float> const by_page = scan_by_shards<4096>(part, ScanKind::Inclusive, shards, stream);
            HeapArray<float> const exclusive_by_grains = scan_by_shards<256>(part, ScanKind::Exclusive, shards, stream);
            HeapArray<float> const sum_by_sixteen =
                scan_by_shards<16>(part, ScanKind::Inclusive, shards, stream, GrainForm::Sum);
            HeapArray<float> const sum_by_page =
                scan_by_shards<4096>(part, ScanKind::Exclusive, shards, stream, GrainForm::Sum);
            assert(same_runs(whole_inclusive, View<float const>{by_single.view()}));
            assert(same_runs(whole_inclusive, View<float const>{by_sixteen.view()}));
            assert(same_runs(whole_inclusive, View<float const>{by_page.view()}));
            assert(same_runs(whole_exclusive, View<float const>{exclusive_by_grains.view()}));
            assert(same_runs(whole_inclusive, View<float const>{sum_by_sixteen.view()}));
            assert(same_runs(whole_exclusive, View<float const>{sum_by_page.view()}));
        }
    }
}

// The error of each output of the float scan stays inside gamma(d) times the
// sum of the magnitudes of its prefix.  The double scan of the same floats is
// the model of the exact prefix.
void test_scan_error_stays_inside_the_bound() {
    constexpr std::size_t kCount = 20000;
    HeapArray<float> const run = draw_floats(kCount, 0x5CA40005u);
    HeapArray<double> wide = ::foundation::core::mint_heap_array(test_alloc(), kCount, 0.0);
    View<double> const wide_view = wide.view();
    std::size_t position = 0;
    for (float const value : run) {
        ::crucible::test::element_at(wide_view, position) = static_cast<double>(value);
        ++position;
    }
    HeapArray<float> out = zeros(kCount);
    HeapArray<double> exact = ::foundation::core::mint_heap_array(test_alloc(), kCount, 0.0);
    assert(scan_into(out, kCount, [&](auto part) noexcept {
        return ::foundation::core::scan_inclusive_sums(View<float const>{run.view()}, std::move(part));
    }));
    assert(scan_into(exact, kCount, [&](auto part) noexcept {
        return ::foundation::core::scan_inclusive_sums(View<double const>{wide_view}, std::move(part));
    }));
    double absolute_sum = 0.0;
    double const unit = 1.0 / 16777216.0;
    for (std::size_t index = 0; index < kCount; ++index) {
        double const value = ::crucible::test::element_at(wide_view, index);
        absolute_sum += value < 0.0 ? -value : value;
        double const depth = ::foundation::core::count_inclusive_scan_depth(index);
        double const gamma = depth * unit / (1.0 - depth * unit);
        double const computed = static_cast<double>(::crucible::test::element_at(out.view(), index));
        double const model = ::crucible::test::element_at(exact.view(), index);
        double const error = computed > model ? computed - model : model - computed;
        assert(error <= gamma * absolute_sum * (1.0 + 1.0e-6));
    }
}

// The plans of larger scans give the outputs of the scans, for a combine
// that is neither associative nor commutative.
void test_large_plans_agree_with_the_scans() {
    auto const mix = [](std::uint64_t const& left, std::uint64_t const& right) noexcept {
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    };
    ::fixy::FixedArray<std::size_t, 5> const counts{127, 128, 129, 1000, 4097};
    ::crucible::test::PhiloxStream stream{0x5CA40006u};
    for (std::size_t const count : counts) {
        HeapArray<std::uint64_t> leaves = ::foundation::core::mint_heap_array(test_alloc(), count, std::uint64_t{0});
        for (std::uint64_t& leaf : leaves) {
            leaf = stream.next_wide();
        }
        View<std::uint64_t const> const in{leaves.view()};
        ::fixy::FixedArray<::foundation::core::ScanKind, 2> const kinds{::foundation::core::ScanKind::Inclusive,
                                                                        ::foundation::core::ScanKind::Exclusive};
        for (::foundation::core::ScanKind const kind : kinds) {
            std::size_t const step_count =
                ::foundation::core::count_scan_plan_steps(count, kind).expect("the count is below the limit");
            HeapArray<::foundation::core::TreeStep> steps =
                ::foundation::core::mint_heap_array(test_alloc(), step_count, ::foundation::core::TreeStep{});
            HeapArray<::foundation::core::PlanNode> outputs =
                ::foundation::core::mint_heap_array(test_alloc(), count, ::foundation::core::PlanNode{});
            steps = ::foundation::core::with_mut(std::move(steps), [&](auto step_out) noexcept {
                outputs = ::foundation::core::with_mut(std::move(outputs), [&](auto output_out) noexcept {
                    assert(
                        ::foundation::core::write_scan_plan(kind, std::move(step_out), std::move(output_out)).is_ok());
                });
            });
            HeapArray<std::uint64_t> scratch =
                ::foundation::core::mint_heap_array(test_alloc(), step_count, std::uint64_t{0});
            HeapArray<std::uint64_t> from_plan =
                ::foundation::core::mint_heap_array(test_alloc(), count, std::uint64_t{0});
            HeapArray<std::uint64_t> from_scan =
                ::foundation::core::mint_heap_array(test_alloc(), count, std::uint64_t{0});
            View<::foundation::core::TreeStep const> const step_view{steps.view()};
            View<::foundation::core::PlanNode const> const output_view{outputs.view()};
            scratch = ::foundation::core::with_mut(std::move(scratch), [&](auto scratch_out) noexcept {
                from_plan = ::foundation::core::with_mut(std::move(from_plan), [&](auto plan_out) noexcept {
                    assert(::foundation::core::evaluate_scan_plan(step_view, output_view, in, std::move(scratch_out),
                                                                  std::move(plan_out), std::uint64_t{0}, mix)
                               .is_ok());
                });
            });
            bool const is_scanned = scan_into(from_scan, count, [&](auto out) noexcept {
                if (kind == ::foundation::core::ScanKind::Inclusive) {
                    return ::foundation::core::scan_inclusive(in, std::move(out), mix);
                }
                return ::foundation::core::scan_exclusive(in, std::move(out), std::uint64_t{0}, mix);
            });
            assert(is_scanned);
            assert(::foundation::core::equal(from_plan.view(), from_scan.view()));
        }
    }
}

// ── The canonical NaN ───────────────────────────────────────────────────

// Each output of out has the output bits of the output of want at its
// position: the canonical NaN for a NaN, and the bits of want for each other
// value.
void check_scan_outputs(View<float const> want, View<float const> out) noexcept {
    for (auto const [model, output] : want.walk().try_zip(out.walk()).expect("the two runs have one count")) {
        assert(::crucible::test::float_bits(*output) == ::crucible::test::output_bits(*model));
    }
}

// Each output of a sum scan that is a NaN is the canonical NaN, and each
// other output keeps the bits of the recursion with an add: for each count of
// lanes, for both kinds and for the grains.  A run of one signaling NaN has
// that NaN as its inclusive output without the rule, and a run of +inf and
// -inf gives 0xFFC00000 on x86.
void test_nan_outputs_of_the_scans_are_canonical() {
    HeapArray<float> run = draw_floats(2100, 0x5CA40040u);
    ::crucible::test::put_specials(run.view(), 0x5CA40041u);
    ::crucible::test::element_at(run.view(), 0) = ::fixy::bit_cast<float>(0x7F800001u);
    ::crucible::test::element_at(run.view(), 1) = ::fixy::bit_cast<float>(0x7F800000u);
    ::crucible::test::element_at(run.view(), 2) = ::fixy::bit_cast<float>(0xFF800000u);
    View<float const> const all{run.view()};
    HeapArray<float> expected = zeros(run.size());
    HeapArray<float> got = zeros(run.size());
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    auto const check_count = [&](std::size_t count, std::size_t first) noexcept {
        View<float const> const part = all.window(first, count).expect("the part fits the run");
        View<float const> const want_view = View<float const>{expected.view()}.window(0, count).expect("the part fits");
        View<float const> const out_view = View<float const>{got.view()}.window(0, count).expect("the part fits");
        assert(scan_into(expected, count, [&](auto want) noexcept {
            return ::foundation::core::scan_inclusive(part, std::move(want), add);
        }));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums(part, std::move(out));
        }));
        check_scan_outputs(want_view, out_view);
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<2>(part, std::move(out));
        }));
        check_scan_outputs(want_view, out_view);
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_inclusive_sums_in_lanes<16>(part, std::move(out));
        }));
        check_scan_outputs(want_view, out_view);
        assert(scan_into(expected, count, [&](auto want) noexcept {
            return ::foundation::core::scan_exclusive(part, std::move(want), 0.0f, add);
        }));
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums(part, std::move(out));
        }));
        check_scan_outputs(want_view, out_view);
        assert(scan_into(got, count, [&](auto out) noexcept {
            return ::foundation::core::scan_exclusive_sums_in_lanes<4>(part, std::move(out));
        }));
        check_scan_outputs(want_view, out_view);
    };
    for (std::size_t count = 1; count <= 300; ++count) {
        check_count(count, 0);
        check_count(count, 2);
    }
    check_count(2100, 0);
    ::crucible::test::PhiloxStream stream{0x5CA40042u};
    HeapArray<float> whole = zeros(all.size());
    assert(scan_into(whole, all.size(),
                     [&](auto out) noexcept { return ::foundation::core::scan_inclusive_sums(all, std::move(out)); }));
    HeapArray<float> const by_grains =
        scan_by_shards<16>(all, ::foundation::core::ScanKind::Inclusive, 4, stream, GrainForm::Sum);
    assert(same_runs(View<float const>{whole.view()}, View<float const>{by_grains.view()}));
    assert(::crucible::test::float_bits(::crucible::test::element_at(whole.view(), 0))
           == ::crucible::test::kCanonicalFloatNan);
}

}  // namespace

int main() {
    test_sum_scans_agree_with_the_naive_fold();
    test_sum_scan_lanes_agree_with_the_recursion();
    test_sum_scan_lanes_keep_the_sign_of_zero();
    test_scan_in_place_and_under_append();
    test_grains_give_the_bits_of_the_scan();
    test_scan_error_stays_inside_the_bound();
    test_large_plans_agree_with_the_scans();
    test_nan_outputs_of_the_scans_are_canonical();
    crucible::test::pass("test_core_scan: all tests passed\n");
    return 0;
}
