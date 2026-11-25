// The golden bits of foundation/core/Reduce.h, Scan.h and Sort.h.  Each form
// runs on inputs that a Philox stream of a fixed seed draws, and the test
// folds the bits of each output into one digest for each form.  The file holds
// the digest of each form.  The library gives the same bits for each target,
// each count of SIMD lanes and each host, so one digest holds for each build.
//
// The other tests compare a fast form with a naive form of the same order.  A
// change of the stated order that changes the two forms together passes them,
// but it changes a digest here.  A change of a draw below also changes the
// digests: write the new digests, with the reason, in the same change.  Take
// each new digest from two builds of different vector widths, and only when
// the two builds agree.  The digest comes from golden_digest.h.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include "golden_digest.h"
#include "nan_outputs.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::fixy::FixedArray;
using ::fixy::HeapArray;
using ::fixy::TextView;
using ::fixy::View;
using ::fixy::with_mut;
using ::crucible::test::PhiloxStream;
using ::crucible::test::golden::fold_bytes;
using ::crucible::test::golden::fold_output;
using ::crucible::test::golden::fold_outputs;
using ::crucible::test::golden::is_golden;
using ::crucible::test::golden::kFnvOffset;
using ::crucible::test::golden::Mix;

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The reference value of FNV-1a for the text "a".
static_assert(fold_bytes(kFnvOffset, 'a', 1) == 0xAF63DC4C8601EC8Cu);

// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, so the association of a sum changes its bits.
[[nodiscard]] float draw_float(PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
    auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
    auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
    return ::fixy::bit_cast<float>((sign << 31) | (exponent << 23) | mantissa);
}

[[nodiscard]] double draw_double(PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    std::uint64_t const mantissa = stream.next_wide() & ((std::uint64_t{1} << 52) - 1);
    std::uint64_t const exponent = 1023 + bits % 40 - 20;
    std::uint64_t const sign = (bits >> 40) & 1u;
    return ::fixy::bit_cast<double>((sign << 63) | (exponent << 52) | mantissa);
}

template <class T>
[[nodiscard]] HeapArray<T> draw_run(std::size_t count, std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    return ::fixy::mint_heap_array_with<T>(test_alloc(), count, [&stream](std::size_t) noexcept {
        if constexpr (sizeof(T) == sizeof(float)) {
            return draw_float(stream);
        } else {
            return draw_double(stream);
        }
    });
}

[[nodiscard]] HeapArray<std::uint64_t> draw_words(std::size_t count, std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    return ::fixy::mint_heap_array_with<std::uint64_t>(test_alloc(), count,
                                                       [&stream](std::size_t) noexcept { return stream.next_wide(); });
}

// The counts of a sum: each count to 70, and the counts around each power of
// two from 2^7 to 2^12.
inline constexpr std::size_t kLongest = (std::size_t{1} << 12) + 3;

template <class Body>
void for_each_count(Body body) noexcept {
    for (std::size_t count = 0; count <= 70; ++count) {
        body(count);
    }
    for (std::size_t power = std::size_t{1} << 7; power <= (std::size_t{1} << 12); power *= 2) {
        for (std::size_t count = power - 3; count <= power + 3; ++count) {
            body(count);
        }
    }
}

// The digest of sum_by_tree of each count of run.
template <class T>
[[nodiscard]] std::uint64_t digest_tree_sums(View<T const> run) noexcept {
    std::uint64_t digest = kFnvOffset;
    for_each_count([&](std::size_t count) noexcept {
        digest = fold_output(digest, ::foundation::core::sum_by_tree(run.window(0, count).expect("the part fits")));
    });
    return digest;
}

// The digest of a form of the tree sum of each count of run.  Each form gives
// the bits of sum_by_tree, so each one gives its golden digest.
template <class T, class Sum>
[[nodiscard]] std::uint64_t digest_tree_sums_by(View<T const> run, Sum sum) noexcept {
    std::uint64_t digest = kFnvOffset;
    for_each_count([&](std::size_t count) noexcept {
        digest = fold_output(digest, sum(run.window(0, count).expect("the part fits")));
    });
    return digest;
}

// The grain of the split forms of the test: a power of two that splits each
// count past 16 into some grains.
inline constexpr std::size_t kGrain = 16;

// The tree sum of a part by grains: the partial of each grain, then
// sum_by_tree of the partials.
template <class T>
[[nodiscard]] T sum_by_grains(View<T const> part) noexcept {
    HeapArray<T> partials =
        ::fixy::mint_heap_array(test_alloc(), ::foundation::core::count_tree_grains<kGrain>(part.size()), T{});
    partials = with_mut(std::move(partials), [part](auto out) noexcept {
        ::foundation::core::sum_grains_by_tree<kGrain>(part, 0, std::move(out));
    });
    return ::foundation::core::sum_by_tree(View<T const>{partials.view()});
}

// The digest of the left fold of each count of run, for the check that the
// digest sees the association of a sum.
[[nodiscard]] std::uint64_t digest_left_folds(View<float const> run) noexcept {
    std::uint64_t digest = kFnvOffset;
    for_each_count([&](std::size_t count) noexcept {
        float sum = 0.0f;
        for (float const value : run.window(0, count).expect("the part fits")) {
            sum += value;
        }
        digest = fold_output(digest, sum);
    });
    return digest;
}

// The digest of reduce_by_tree with the combine Mix of each count of words.
[[nodiscard]] std::uint64_t digest_tree_reduces(View<std::uint64_t const> words) noexcept {
    std::uint64_t digest = kFnvOffset;
    for_each_count([&](std::size_t count) noexcept {
        if (count == 0) return;
        View<std::uint64_t const> const part = words.window(0, count).expect("the part fits");
        digest = fold_output(digest, ::foundation::core::reduce_by_tree(part, Mix{}).expect("the part has elements"));
    });
    return digest;
}

// The digest of reduce_grains_by_tree with the combine Mix of each count of
// words, then reduce_by_tree of the partials: the bits of reduce_by_tree.
[[nodiscard]] std::uint64_t digest_grain_reduces(View<std::uint64_t const> words) noexcept {
    std::uint64_t digest = kFnvOffset;
    for_each_count([&](std::size_t count) noexcept {
        if (count == 0) return;
        View<std::uint64_t const> const part = words.window(0, count).expect("the part fits");
        HeapArray<std::uint64_t> partials = ::fixy::mint_heap_array(
            test_alloc(), ::foundation::core::count_tree_grains<kGrain>(count), std::uint64_t{0});
        partials = with_mut(std::move(partials), [&](auto out) noexcept {
            ::foundation::core::reduce_grains_by_tree<kGrain>(part, 0, std::move(out), Mix{});
        });
        digest =
            fold_output(digest, ::foundation::core::reduce_by_tree(View<std::uint64_t const>{partials.view()}, Mix{})
                                    .expect("the part has grains"));
    });
    return digest;
}

// The digest of sum_runs_by_tree of the first K of 9 runs of 61 elements, for
// each K from 1 to 9.
inline constexpr std::size_t kRuns = 9;
inline constexpr std::size_t kRunCount = 61;

[[nodiscard]] std::uint64_t digest_run_sums(View<float const> elements) noexcept {
    FixedArray<View<float const>, kRuns> runs{};
    for (std::size_t run = 0; run < kRuns; ++run) {
        View<float const> const window = elements.window(run * kRunCount, kRunCount).expect("the run fits");
        runs.set(::foundation::core::Index<kRuns>::check(run).expect(
                     "a test reads an element at or past the count of its array"),
                 window);
    }
    std::uint64_t digest = kFnvOffset;
    for (std::size_t first_runs = 1; first_runs <= kRuns; ++first_runs) {
        View<View<float const> const> const chosen =
            View<View<float const> const>{runs.view()}.window(0, first_runs).expect("the runs fit");
        auto const [is_summed, sums] =
            ::foundation::core::build_fixed_array<float, kRunCount>([&chosen](auto out) noexcept {
                return ::foundation::core::sum_runs_by_tree(chosen, std::move(out)).is_ok();
            });
        CRUCIBLE_TEST_ASSERT(kTest, is_summed);
        digest = fold_outputs(digest, View<float const>{sums.view()});
    }
    return digest;
}

// The digest of sum_segments_by_tree of 80 segments with drawn counts from 0
// to 48.
inline constexpr std::size_t kSegments = 80;

[[nodiscard]] std::uint64_t digest_segment_sums(std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    std::size_t end = 0;
    HeapArray<std::size_t> const offsets = ::fixy::mint_heap_array_with<std::size_t>(
        test_alloc(), kSegments + 1, [&stream, &end](std::size_t segment) noexcept {
            if (segment != 0) end += stream.below(49);
            return end;
        });
    HeapArray<float> const elements = draw_run<float>(end, seed + 1);
    HeapArray<float> sums = ::fixy::mint_heap_array(test_alloc(), kSegments, 0.0f);
    auto [is_summed, written] = with_mut(std::move(sums), [&](auto out) noexcept {
        return ::foundation::core::sum_segments_by_tree(View<float const>{elements.view()},
                                                        View<std::size_t const>{offsets.view()}, std::move(out))
            .is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_summed);
    return fold_outputs(kFnvOffset, View<float const>{written.view()});
}

// The digest of the inclusive and then the exclusive scan of the sums of in.
template <class T>
[[nodiscard]] std::uint64_t digest_sum_scans(View<T const> in) noexcept {
    HeapArray<T> inclusive = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    HeapArray<T> exclusive = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    auto [is_inclusive, inclusive_back] = with_mut(std::move(inclusive), [in](auto out) noexcept {
        return ::foundation::core::scan_inclusive_sums(in, std::move(out)).is_ok();
    });
    auto [is_exclusive, exclusive_back] = with_mut(std::move(exclusive), [in](auto out) noexcept {
        return ::foundation::core::scan_exclusive_sums(in, std::move(out)).is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_inclusive && is_exclusive);
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<T const>{inclusive_back.view()});
    return fold_outputs(digest, View<T const>{exclusive_back.view()});
}

// The digest of the inclusive and then the exclusive scan of words with the
// combine Mix.
[[nodiscard]] std::uint64_t digest_mix_scans(View<std::uint64_t const> words) noexcept {
    HeapArray<std::uint64_t> inclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    HeapArray<std::uint64_t> exclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    auto [is_inclusive, inclusive_back] = with_mut(std::move(inclusive), [words](auto out) noexcept {
        return ::foundation::core::scan_inclusive(words, std::move(out), Mix{}).is_ok();
    });
    auto [is_exclusive, exclusive_back] = with_mut(std::move(exclusive), [words](auto out) noexcept {
        return ::foundation::core::scan_exclusive(words, std::move(out), std::uint64_t{0x5CA9u}, Mix{}).is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_inclusive && is_exclusive);
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<std::uint64_t const>{inclusive_back.view()});
    return fold_outputs(digest, View<std::uint64_t const>{exclusive_back.view()});
}

// The digest of reduce_segments_by_tree with the add of floats on the
// segments of digest_segment_sums.  The run holds no NaN, so the form gives
// the bits of sum_segments_by_tree and its golden digest.
struct AddFloats {
    [[nodiscard]] constexpr float operator()(float const& left, float const& right) const noexcept {
        return left + right;
    }
};

[[nodiscard]] std::uint64_t digest_segment_reduces(std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    std::size_t end = 0;
    HeapArray<std::size_t> const offsets = ::fixy::mint_heap_array_with<std::size_t>(
        test_alloc(), kSegments + 1, [&stream, &end](std::size_t segment) noexcept {
            if (segment != 0) end += stream.below(49);
            return end;
        });
    HeapArray<float> const elements = draw_run<float>(end, seed + 1);
    HeapArray<float> sums = ::fixy::mint_heap_array(test_alloc(), kSegments, 0.0f);
    auto [is_reduced, written] = with_mut(std::move(sums), [&](auto out) noexcept {
        return ::foundation::core::reduce_segments_by_tree(View<float const>{elements.view()},
                                                           View<std::size_t const>{offsets.view()}, std::move(out),
                                                           0.0f, AddFloats{})
            .is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_reduced);
    return fold_outputs(kFnvOffset, View<float const>{written.view()});
}

// The digest of the inclusive and then the exclusive scan of the sums of in,
// with the two scan forms of the caller.  Each form gives the bits of the sum
// scans, so each one gives their golden digest.
template <class T, class Inclusive, class Exclusive>
[[nodiscard]] std::uint64_t digest_sum_scans_by(View<T const> in, Inclusive inclusive, Exclusive exclusive) noexcept {
    HeapArray<T> inclusive_out = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    HeapArray<T> exclusive_out = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    inclusive_out =
        with_mut(std::move(inclusive_out), [in, inclusive](auto out) noexcept { inclusive(in, std::move(out)); });
    exclusive_out =
        with_mut(std::move(exclusive_out), [in, exclusive](auto out) noexcept { exclusive(in, std::move(out)); });
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<T const>{inclusive_out.view()});
    return fold_outputs(digest, View<T const>{exclusive_out.view()});
}

// The carry of each grain of in: the exclusive sum scan of the tree sums of
// the grains.  The carries read no sum of the last grain.
template <class T>
[[nodiscard]] HeapArray<T> sum_carries_of_grains(View<T const> in) noexcept {
    std::size_t const grains = ::foundation::core::count_tree_grains<kGrain>(in.size());
    HeapArray<T> sums = ::fixy::mint_heap_array(test_alloc(), grains, T{});
    sums = with_mut(std::move(sums),
                    [in](auto out) noexcept { ::foundation::core::sum_grains_by_tree<kGrain>(in, 0, std::move(out)); });
    HeapArray<T> carries = ::fixy::mint_heap_array(test_alloc(), grains, T{});
    bool is_scanned = false;
    carries = with_mut(std::move(carries), [&](auto out) noexcept {
        is_scanned = ::foundation::core::scan_exclusive_sums(View<T const>{sums.view()}, std::move(out)).is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    return carries;
}

// The digest of the scans of words with the combine Mix by grains, with the
// carries of the exclusive scan of the tree reduces of the grains: the bits of
// the scans with Mix.
[[nodiscard]] std::uint64_t digest_mix_grain_scans(View<std::uint64_t const> words) noexcept {
    std::size_t const grains = ::foundation::core::count_tree_grains<kGrain>(words.size());
    HeapArray<std::uint64_t> reduces = ::fixy::mint_heap_array(test_alloc(), grains, std::uint64_t{0});
    reduces = with_mut(std::move(reduces), [words](auto out) noexcept {
        ::foundation::core::reduce_grains_by_tree<kGrain>(words, 0, std::move(out), Mix{});
    });
    HeapArray<std::uint64_t> carries = ::fixy::mint_heap_array(test_alloc(), grains, std::uint64_t{0});
    bool is_scanned = false;
    carries = with_mut(std::move(carries), [&](auto out) noexcept {
        is_scanned = ::foundation::core::scan_exclusive(View<std::uint64_t const>{reduces.view()}, std::move(out),
                                                        std::uint64_t{0x5CA9u}, Mix{})
                         .is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    View<std::uint64_t const> const carried{carries.view()};
    HeapArray<std::uint64_t> inclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    HeapArray<std::uint64_t> exclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    inclusive = with_mut(std::move(inclusive), [words, carried](auto out) noexcept {
        ::foundation::core::scan_inclusive_grains<kGrain>(words, 0, carried, std::move(out), Mix{});
    });
    exclusive = with_mut(std::move(exclusive), [words, carried](auto out) noexcept {
        ::foundation::core::scan_exclusive_grains<kGrain>(words, 0, carried, std::move(out), std::uint64_t{0x5CA9u},
                                                          Mix{});
    });
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<std::uint64_t const>{inclusive.view()});
    return fold_outputs(digest, View<std::uint64_t const>{exclusive.view()});
}

// sort_stably of a copy of elements, and the digest of the sorted run.
template <class T, class... Less>
[[nodiscard]] std::uint64_t digest_stable_sort(View<T const> elements, Less... less) noexcept {
    HeapArray<T> sorted =
        ::fixy::mint_heap_array_with<T>(test_alloc(), elements.size(), [elements](std::size_t position) noexcept {
            return *elements.get(position).expect("a test reads an element at or past the count of its View");
        });
    HeapArray<T> scratch = ::fixy::mint_heap_array(test_alloc(), elements.size(), T{});
    bool is_sorted = false;
    sorted = with_mut(std::move(sorted), [&](auto out) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(out), std::move(room), less...).is_ok();
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_sorted);
    return fold_outputs(kFnvOffset, View<T const>{sorted.view()});
}

// A word with a key of 4 bits in its high half and its position in the input
// in its low half.  The order compares the keys only, so the low halves of
// the sorted run show the stable order of the equal keys.
[[nodiscard]] HeapArray<std::uint64_t> draw_keyed(std::size_t count, std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    std::uint64_t position = 0;
    return ::fixy::mint_heap_array_with<std::uint64_t>(test_alloc(), count, [&stream, &position](std::size_t) noexcept {
        return (stream.below(16) << 32) | position++;
    });
}

struct KeyLess {
    [[nodiscard]] constexpr bool operator()(std::uint64_t const& left, std::uint64_t const& right) const noexcept {
        return (left >> 32) < (right >> 32);
    }
};

// sort_by_network of N drawn words, and the digest of the sorted words.
template <std::size_t N>
[[nodiscard]] std::uint64_t fold_network_sort(std::uint64_t digest, PhiloxStream& stream) noexcept {
    auto [values, values_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<std::uint64_t, N>);
    values.with_mut(values_key, [&stream](auto cells) noexcept {
        for (std::uint64_t& value : cells) {
            value = stream.below(64);
        }
    });
    values.with_mut(values_key, [](auto out) noexcept { ::foundation::core::sort_by_network(std::move(out)); });
    return fold_outputs(digest, View<std::uint64_t const>{values.view()});
}

[[nodiscard]] std::uint64_t digest_network_sorts(std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    std::uint64_t digest = kFnvOffset;
    digest = fold_network_sort<2>(digest, stream);
    digest = fold_network_sort<3>(digest, stream);
    digest = fold_network_sort<7>(digest, stream);
    digest = fold_network_sort<16>(digest, stream);
    digest = fold_network_sort<33>(digest, stream);
    return fold_network_sort<256>(digest, stream);
}

void test_golden_bits() {
    HeapArray<float> const floats = draw_run<float>(kLongest, 0x601D0001u);
    HeapArray<double> const doubles = draw_run<double>(kLongest, 0x601D0002u);
    HeapArray<float> special_floats = draw_run<float>(kLongest, 0x601D0003u);
    special_floats = with_mut(std::move(special_floats),
                              [](auto cells) noexcept { ::crucible::test::put_specials(cells, 0x601D0004u); });
    HeapArray<double> special_doubles = draw_run<double>(kLongest, 0x601D0005u);
    special_doubles = with_mut(std::move(special_doubles),
                               [](auto cells) noexcept { ::crucible::test::put_specials(cells, 0x601D0006u); });
    HeapArray<std::uint64_t> const words = draw_words(kLongest, 0x601D0007u);
    HeapArray<float> const run_elements = draw_run<float>(kRuns * kRunCount, 0x601D0008u);
    HeapArray<std::uint64_t> const keyed = draw_keyed(1000, 0x601D000Au);
    HeapArray<float> sort_floats = draw_run<float>(1000, 0x601D000Bu);
    sort_floats = with_mut(std::move(sort_floats), [](auto cells) noexcept {
        ::crucible::test::put_specials(cells, 0x601D000Cu);
        auto first = cells.get_mut(0).expect("a test reads an element at or past the count of its View");
        *first = -0.0f;
        auto second = cells.get_mut(1).expect("a test reads an element at or past the count of its View");
        *second = 0.0f;
    });

    View<float const> const float_view{floats.view()};
    View<float const> const special_float_view{special_floats.view()};
    View<double const> const special_double_view{special_doubles.view()};
    View<std::uint64_t const> const word_view{words.view()};
    View<float const> const scan_floats = float_view.window(0, 1000).expect("the scan run fits");
    View<double const> const scan_doubles = View<double const>{doubles.view()}.window(0, 1000).expect("it fits");
    View<std::uint64_t const> const scan_words = word_view.window(0, 300).expect("the scan run fits");

    // The digest sees the association of a sum: the left fold gives another
    // digest than the tree.
    CRUCIBLE_TEST_ASSERT(kTest, digest_left_folds(float_view) != digest_tree_sums(float_view));

    // Each form prints its digest when it differs, so one run gives each
    // digest that differs.
    std::size_t differences = 0;
    auto const compare = [&](TextView form, std::uint64_t digest, std::uint64_t golden) noexcept {
        if (!is_golden(kTest, form, digest, golden)) ++differences;
    };
    View<double const> const double_view{doubles.view()};
    View<float const> const special_scan_floats = special_float_view.window(0, 1000).expect("the scan run fits");
    constexpr std::uint64_t golden_float_sums = 0x6BAC76EA698CE629u;
    constexpr std::uint64_t golden_double_sums = 0x879105B969D69A92u;
    constexpr std::uint64_t golden_special_float_sums = 0xB5F2534EF8243125u;
    constexpr std::uint64_t golden_special_double_sums = 0x790612CBE499BEF2u;
    constexpr std::uint64_t golden_reduces = 0xD4264F25E1C73CE7u;
    constexpr std::uint64_t golden_segment_sums = 0xB97DD88541040B89u;
    constexpr std::uint64_t golden_float_scans = 0xFFDC5DA43D51C2F3u;
    constexpr std::uint64_t golden_double_scans = 0x0FD4EF291E8301FAu;
    constexpr std::uint64_t golden_special_float_scans = 0x8632DFF569B8D39Cu;
    constexpr std::uint64_t golden_mix_scans = 0x20CCA23B780FD991u;
    compare("sum_by_tree of float", digest_tree_sums(float_view), golden_float_sums);
    compare("sum_by_tree of double", digest_tree_sums(double_view), golden_double_sums);
    compare("sum_by_tree of float with NaNs", digest_tree_sums(special_float_view), golden_special_float_sums);
    compare("sum_by_tree of double with NaNs", digest_tree_sums(special_double_view), golden_special_double_sums);
    compare("reduce_by_tree with a mix", digest_tree_reduces(word_view), golden_reduces);
    compare("sum_runs_by_tree", digest_run_sums(View<float const>{run_elements.view()}), 0xF2B81C6AA6EC7507u);
    compare("sum_segments_by_tree", digest_segment_sums(0x601D0009u), golden_segment_sums);
    compare("the sum scans of float", digest_sum_scans(scan_floats), golden_float_scans);
    compare("the sum scans of double", digest_sum_scans(scan_doubles), golden_double_scans);
    compare("the sum scans of float with NaNs", digest_sum_scans(special_scan_floats), golden_special_float_scans);
    compare("the scans with a mix", digest_mix_scans(scan_words), golden_mix_scans);

    // The lane forms, the grain forms and the forms with the combine of a
    // caller state the bits of a form above, so each one gives its golden
    // digest.
    auto const in_four_lanes = [](auto part) noexcept { return ::foundation::core::sum_by_tree_in_lanes<4>(part); };
    auto const in_eight_lanes = [](auto part) noexcept { return ::foundation::core::sum_by_tree_in_lanes<8>(part); };
    auto const in_sixteen_lanes = [](auto part) noexcept { return ::foundation::core::sum_by_tree_in_lanes<16>(part); };
    auto const by_grains = [](auto part) noexcept { return sum_by_grains(part); };
    compare("sum_by_tree_in_lanes<4> of float", digest_tree_sums_by(float_view, in_four_lanes), golden_float_sums);
    compare("sum_by_tree_in_lanes<8> of float", digest_tree_sums_by(float_view, in_eight_lanes), golden_float_sums);
    compare("sum_by_tree_in_lanes<16> of float", digest_tree_sums_by(float_view, in_sixteen_lanes), golden_float_sums);
    compare("sum_by_tree_in_lanes<8> of double", digest_tree_sums_by(double_view, in_eight_lanes), golden_double_sums);
    compare("sum_by_tree_in_lanes<16> of float with NaNs", digest_tree_sums_by(special_float_view, in_sixteen_lanes),
            golden_special_float_sums);
    compare("sum_grains_by_tree of float", digest_tree_sums_by(float_view, by_grains), golden_float_sums);
    compare("sum_grains_by_tree of double", digest_tree_sums_by(double_view, by_grains), golden_double_sums);
    compare("sum_grains_by_tree of float with NaNs", digest_tree_sums_by(special_float_view, by_grains),
            golden_special_float_sums);
    compare("sum_grains_by_tree of double with NaNs", digest_tree_sums_by(special_double_view, by_grains),
            golden_special_double_sums);
    compare("reduce_grains_by_tree with a mix", digest_grain_reduces(word_view), golden_reduces);
    compare("reduce_segments_by_tree with an add", digest_segment_reduces(0x601D0009u), golden_segment_sums);

    auto const inclusive_in_four_lanes = [](auto in, auto out) noexcept {
        bool const is_scanned = ::foundation::core::scan_inclusive_sums_in_lanes<4>(in, std::move(out)).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    };
    auto const exclusive_in_four_lanes = [](auto in, auto out) noexcept {
        bool const is_scanned = ::foundation::core::scan_exclusive_sums_in_lanes<4>(in, std::move(out)).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    };
    auto const inclusive_in_sixteen_lanes = [](auto in, auto out) noexcept {
        bool const is_scanned = ::foundation::core::scan_inclusive_sums_in_lanes<16>(in, std::move(out)).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    };
    auto const exclusive_in_sixteen_lanes = [](auto in, auto out) noexcept {
        bool const is_scanned = ::foundation::core::scan_exclusive_sums_in_lanes<16>(in, std::move(out)).is_ok();
        CRUCIBLE_TEST_ASSERT(kTest, is_scanned);
    };
    auto const inclusive_by_grains = [](auto in, auto out) noexcept {
        auto const carries = sum_carries_of_grains(in);
        ::foundation::core::scan_inclusive_sum_grains<kGrain>(in, 0, carries.view(), std::move(out));
    };
    auto const exclusive_by_grains = [](auto in, auto out) noexcept {
        auto const carries = sum_carries_of_grains(in);
        ::foundation::core::scan_exclusive_sum_grains<kGrain>(in, 0, carries.view(), std::move(out));
    };
    compare("the sum scans in 4 lanes of float",
            digest_sum_scans_by(scan_floats, inclusive_in_four_lanes, exclusive_in_four_lanes), golden_float_scans);
    compare("the sum scans in 16 lanes of double",
            digest_sum_scans_by(scan_doubles, inclusive_in_sixteen_lanes, exclusive_in_sixteen_lanes),
            golden_double_scans);
    compare("the sum scans in 16 lanes of float with NaNs",
            digest_sum_scans_by(special_scan_floats, inclusive_in_sixteen_lanes, exclusive_in_sixteen_lanes),
            golden_special_float_scans);
    compare("the sum scans by grains of float",
            digest_sum_scans_by(scan_floats, inclusive_by_grains, exclusive_by_grains), golden_float_scans);
    compare("the sum scans by grains of double",
            digest_sum_scans_by(scan_doubles, inclusive_by_grains, exclusive_by_grains), golden_double_scans);
    compare("the sum scans by grains of float with NaNs",
            digest_sum_scans_by(special_scan_floats, inclusive_by_grains, exclusive_by_grains),
            golden_special_float_scans);
    compare("the scans with a mix by grains", digest_mix_grain_scans(scan_words), golden_mix_scans);
    compare("sort_stably by a key", digest_stable_sort(View<std::uint64_t const>{keyed.view()}, KeyLess{}),
            0x85C650B5059457A4u);
    compare("sort_stably of float", digest_stable_sort(View<float const>{sort_floats.view()}), 0xE43981347DE9CFCBu);
    compare("sort_by_network", digest_network_sorts(0x601D000Du), 0x88F4B8269182E8FFu);
    CRUCIBLE_TEST_ASSERT(kTest, differences == 0);
}

}  // namespace

int main() {
    test_golden_bits();
    ::fixy::check::pass(kTest, "test_core_golden_bits: all tests passed\n");
    return 0;
}
