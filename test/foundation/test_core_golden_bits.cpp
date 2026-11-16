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
// the two builds agree.

#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"
#include "nan_outputs.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::fixy::FixedArray;
using ::fixy::HeapArray;
using ::fixy::TextView;
using ::fixy::View;
using ::fixy::with_mut;
using ::crucible::test::element_at;
using ::crucible::test::float_bits;
using ::crucible::test::PhiloxStream;

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The FNV-1a hash of 64 bits.  A word adds its bytes from the low byte.
inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325u;
inline constexpr std::uint64_t kFnvPrime = 0x100000001B3u;

// Adds the count low bytes of word to digest.
[[nodiscard]] constexpr std::uint64_t fold_bytes(std::uint64_t digest, std::uint64_t word, int count) noexcept {
    for (int position = 0; position < count; ++position) {
        digest = (digest ^ ((word >> (8 * position)) & 0xFFu)) * kFnvPrime;
    }
    return digest;
}

// The reference value of FNV-1a for the text "a".
static_assert(fold_bytes(kFnvOffset, 'a', 1) == 0xAF63DC4C8601EC8Cu);

[[nodiscard]] std::uint64_t fold_output(std::uint64_t digest, float value) noexcept {
    return fold_bytes(digest, float_bits(value), 4);
}
[[nodiscard]] std::uint64_t fold_output(std::uint64_t digest, double value) noexcept {
    return fold_bytes(digest, float_bits(value), 8);
}
[[nodiscard]] std::uint64_t fold_output(std::uint64_t digest, std::uint64_t value) noexcept {
    return fold_bytes(digest, value, 8);
}

template <class T>
[[nodiscard]] std::uint64_t fold_outputs(std::uint64_t digest, View<T const> outputs) noexcept {
    for (T const value : outputs) {
        digest = fold_output(digest, value);
    }
    return digest;
}

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
    HeapArray<T> run = ::fixy::mint_heap_array(test_alloc(), count, T{});
    PhiloxStream stream{seed};
    for (T& value : run) {
        if constexpr (sizeof(T) == sizeof(float)) {
            value = draw_float(stream);
        } else {
            value = draw_double(stream);
        }
    }
    return run;
}

[[nodiscard]] HeapArray<std::uint64_t> draw_words(std::size_t count, std::uint64_t seed) noexcept {
    HeapArray<std::uint64_t> run = ::fixy::mint_heap_array(test_alloc(), count, std::uint64_t{0});
    PhiloxStream stream{seed};
    for (std::uint64_t& word : run) {
        word = stream.next_wide();
    }
    return run;
}

// A combine that is neither associative nor commutative: a change of the
// shape of the tree or of the order of two operands changes its result.
struct Mix {
    [[nodiscard]] constexpr std::uint64_t operator()(std::uint64_t const& left,
                                                     std::uint64_t const& right) const noexcept {
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }
};

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

// The digest of sum_runs_by_tree of the first K of 9 runs of 61 elements, for
// each K from 1 to 9.
inline constexpr std::size_t kRuns = 9;
inline constexpr std::size_t kRunCount = 61;

[[nodiscard]] std::uint64_t digest_run_sums(View<float const> elements) noexcept {
    FixedArray<View<float const>, kRuns> runs{};
    for (std::size_t run = 0; run < kRuns; ++run) {
        element_at(runs, run) = elements.window(run * kRunCount, kRunCount).expect("the run fits");
    }
    std::uint64_t digest = kFnvOffset;
    for (std::size_t first_runs = 1; first_runs <= kRuns; ++first_runs) {
        View<View<float const> const> const chosen =
            View<View<float const> const>{runs.view()}.window(0, first_runs).expect("the runs fit");
        FixedArray<float, kRunCount> sums{};
        auto [is_summed, written] = with_mut(std::move(sums), [&](auto out) noexcept {
            return ::foundation::core::sum_runs_by_tree(chosen, std::move(out)).is_ok();
        });
        assert(is_summed);
        digest = fold_outputs(digest, View<float const>{written.view()});
    }
    return digest;
}

// The digest of sum_segments_by_tree of 80 segments with drawn counts from 0
// to 48.
inline constexpr std::size_t kSegments = 80;

[[nodiscard]] std::uint64_t digest_segment_sums(std::uint64_t seed) noexcept {
    HeapArray<std::size_t> offsets = ::fixy::mint_heap_array(test_alloc(), kSegments + 1, std::size_t{0});
    PhiloxStream stream{seed};
    std::size_t end = 0;
    for (std::size_t segment = 1; segment <= kSegments; ++segment) {
        end += stream.below(49);
        element_at(offsets.view(), segment) = end;
    }
    HeapArray<float> const elements = draw_run<float>(end, seed + 1);
    HeapArray<float> sums = ::fixy::mint_heap_array(test_alloc(), kSegments, 0.0f);
    auto [is_summed, written] = with_mut(std::move(sums), [&](auto out) noexcept {
        return ::foundation::core::sum_segments_by_tree(View<float const>{elements.view()},
                                                        View<std::size_t const>{offsets.view()}, std::move(out))
            .is_ok();
    });
    assert(is_summed);
    return fold_outputs(kFnvOffset, View<float const>{written.view()});
}

// The digest of the inclusive and then the exclusive scan of the sums of in.
template <class T>
[[nodiscard]] std::uint64_t digest_sum_scans(View<T const> in) noexcept {
    HeapArray<T> inclusive = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    HeapArray<T> exclusive = ::fixy::mint_heap_array(test_alloc(), in.size(), T{});
    auto [is_inclusive, inclusive_back] = with_mut(std::move(inclusive), [&](auto out) noexcept {
        return ::foundation::core::scan_inclusive_sums(in, std::move(out)).is_ok();
    });
    auto [is_exclusive, exclusive_back] = with_mut(std::move(exclusive), [&](auto out) noexcept {
        return ::foundation::core::scan_exclusive_sums(in, std::move(out)).is_ok();
    });
    assert(is_inclusive && is_exclusive);
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<T const>{inclusive_back.view()});
    return fold_outputs(digest, View<T const>{exclusive_back.view()});
}

// The digest of the inclusive and then the exclusive scan of words with the
// combine Mix.
[[nodiscard]] std::uint64_t digest_mix_scans(View<std::uint64_t const> words) noexcept {
    HeapArray<std::uint64_t> inclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    HeapArray<std::uint64_t> exclusive = ::fixy::mint_heap_array(test_alloc(), words.size(), std::uint64_t{0});
    auto [is_inclusive, inclusive_back] = with_mut(std::move(inclusive), [&](auto out) noexcept {
        return ::foundation::core::scan_inclusive(words, std::move(out), Mix{}).is_ok();
    });
    auto [is_exclusive, exclusive_back] = with_mut(std::move(exclusive), [&](auto out) noexcept {
        return ::foundation::core::scan_exclusive(words, std::move(out), std::uint64_t{0x5CA9u}, Mix{}).is_ok();
    });
    assert(is_inclusive && is_exclusive);
    std::uint64_t const digest = fold_outputs(kFnvOffset, View<std::uint64_t const>{inclusive_back.view()});
    return fold_outputs(digest, View<std::uint64_t const>{exclusive_back.view()});
}

// sort_stably of a copy of elements, and the digest of the sorted run.
template <class T, class... Less>
[[nodiscard]] std::uint64_t digest_stable_sort(View<T const> elements, Less... less) noexcept {
    HeapArray<T> sorted = ::fixy::mint_heap_array(test_alloc(), elements.size(), T{});
    HeapArray<T> scratch = ::fixy::mint_heap_array(test_alloc(), elements.size(), T{});
    std::size_t position = 0;
    for (T& value : sorted) {
        value = element_at(elements, position++);
    }
    bool is_sorted = false;
    sorted = with_mut(std::move(sorted), [&](auto out) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(out), std::move(room), less...).is_ok();
        });
    });
    assert(is_sorted);
    return fold_outputs(kFnvOffset, View<T const>{sorted.view()});
}

// A word with a key of 4 bits in its high half and its position in the input
// in its low half.  The order compares the keys only, so the low halves of
// the sorted run show the stable order of the equal keys.
[[nodiscard]] HeapArray<std::uint64_t> draw_keyed(std::size_t count, std::uint64_t seed) noexcept {
    HeapArray<std::uint64_t> run = ::fixy::mint_heap_array(test_alloc(), count, std::uint64_t{0});
    PhiloxStream stream{seed};
    std::uint64_t position = 0;
    for (std::uint64_t& word : run) {
        word = (stream.below(16) << 32) | position++;
    }
    return run;
}

struct KeyLess {
    [[nodiscard]] constexpr bool operator()(std::uint64_t const& left, std::uint64_t const& right) const noexcept {
        return (left >> 32) < (right >> 32);
    }
};

// sort_by_network of N drawn words, and the digest of the sorted words.
template <std::size_t N>
[[nodiscard]] std::uint64_t fold_network_sort(std::uint64_t digest, PhiloxStream& stream) noexcept {
    FixedArray<std::uint64_t, N> values{};
    for (std::uint64_t& value : values) {
        value = stream.below(64);
    }
    values =
        with_mut(std::move(values), [](auto out) noexcept { ::foundation::core::sort_by_network(std::move(out)); });
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

// Compares the digest of one form with its golden digest, and prints the two
// when they differ.
[[nodiscard]] bool is_golden(TextView form, std::uint64_t digest, std::uint64_t golden) noexcept {
    if (digest == golden) return true;
    ::crucible::test::pass("test_core_golden_bits: the digest of {} is {}, and the golden digest is {}\n", form, digest,
                           golden);
    return false;
}

void test_golden_bits() {
    HeapArray<float> const floats = draw_run<float>(kLongest, 0x601D0001u);
    HeapArray<double> const doubles = draw_run<double>(kLongest, 0x601D0002u);
    HeapArray<float> special_floats = draw_run<float>(kLongest, 0x601D0003u);
    ::crucible::test::put_specials(special_floats.view(), 0x601D0004u);
    HeapArray<double> special_doubles = draw_run<double>(kLongest, 0x601D0005u);
    ::crucible::test::put_specials(special_doubles.view(), 0x601D0006u);
    HeapArray<std::uint64_t> const words = draw_words(kLongest, 0x601D0007u);
    HeapArray<float> const run_elements = draw_run<float>(kRuns * kRunCount, 0x601D0008u);
    HeapArray<std::uint64_t> const keyed = draw_keyed(1000, 0x601D000Au);
    HeapArray<float> sort_floats = draw_run<float>(1000, 0x601D000Bu);
    ::crucible::test::put_specials(sort_floats.view(), 0x601D000Cu);
    element_at(sort_floats.view(), 0) = -0.0f;
    element_at(sort_floats.view(), 1) = 0.0f;

    View<float const> const float_view{floats.view()};
    View<float const> const special_float_view{special_floats.view()};
    View<double const> const special_double_view{special_doubles.view()};
    View<std::uint64_t const> const word_view{words.view()};
    View<float const> const scan_floats = float_view.window(0, 1000).expect("the scan run fits");
    View<double const> const scan_doubles = View<double const>{doubles.view()}.window(0, 1000).expect("it fits");
    View<std::uint64_t const> const scan_words = word_view.window(0, 300).expect("the scan run fits");

    // The digest sees the association of a sum: the left fold gives another
    // digest than the tree.
    assert(digest_left_folds(float_view) != digest_tree_sums(float_view));

    // Each form prints its digest when it differs, so one run gives each
    // digest that differs.
    std::size_t differences = 0;
    auto const compare = [&](TextView form, std::uint64_t digest, std::uint64_t golden) noexcept {
        if (!is_golden(form, digest, golden)) ++differences;
    };
    compare("sum_by_tree of float", digest_tree_sums(float_view), 0x6BAC76EA698CE629u);
    compare("sum_by_tree of double", digest_tree_sums(View<double const>{doubles.view()}), 0x879105B969D69A92u);
    compare("sum_by_tree of float with NaNs", digest_tree_sums(special_float_view), 0xB5F2534EF8243125u);
    compare("sum_by_tree of double with NaNs", digest_tree_sums(special_double_view), 0x790612CBE499BEF2u);
    compare("reduce_by_tree with a mix", digest_tree_reduces(word_view), 0xD4264F25E1C73CE7u);
    compare("sum_runs_by_tree", digest_run_sums(View<float const>{run_elements.view()}), 0xF2B81C6AA6EC7507u);
    compare("sum_segments_by_tree", digest_segment_sums(0x601D0009u), 0xB97DD88541040B89u);
    compare("the sum scans of float", digest_sum_scans(scan_floats), 0xFFDC5DA43D51C2F3u);
    compare("the sum scans of double", digest_sum_scans(scan_doubles), 0x0FD4EF291E8301FAu);
    compare("the sum scans of float with NaNs",
            digest_sum_scans(special_float_view.window(0, 1000).expect("the scan run fits")), 0x8632DFF569B8D39Cu);
    compare("the scans with a mix", digest_mix_scans(scan_words), 0x20CCA23B780FD991u);
    compare("sort_stably by a key", digest_stable_sort(View<std::uint64_t const>{keyed.view()}, KeyLess{}),
            0x85C650B5059457A4u);
    compare("sort_stably of float", digest_stable_sort(View<float const>{sort_floats.view()}), 0xE43981347DE9CFCBu);
    compare("sort_by_network", digest_network_sorts(0x601D000Du), 0x88F4B8269182E8FFu);
    assert(differences == 0);
}

}  // namespace

int main() {
    test_golden_bits();
    crucible::test::pass("test_core_golden_bits: all tests passed\n");
    return 0;
}
