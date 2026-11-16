// Microbench of the tree reduce, the scans and the sorts of foundation/core
// on one thread.
//
// Each row is one call.  The rows of 1024 and 65536 floats read data that
// the private cache of the core holds after the first call, the row of 2^20
// floats reads the L3, and the row of 2^24 floats reads memory.  The left
// fold is the order that the tree replaces: one chain of adds, which the
// latency of an add bounds.  A sort row copies its input into the work
// buffer first, so the time of each sort row holds one copy.  A stable sort
// by Ascending of at least stable_sort_radix_count elements takes the radix
// sort, and a row by an order of the caller takes the merge sort.  The row of
// a sorted input shows the merge of two runs in order.  Each call that
// writes takes its output through a frame door: a HeapArray moves in and out,
// and the FixedArray of a network row copies its N elements in and out.
//
// Each line gives the core of its row (the core that the harness reads after
// its pin, or cpu-1 for a row with no pin), the percentiles of one call in
// nanoseconds, the coefficient of variation in thousandths, and the rate in
// elements for each microsecond at the median.  The Report family writes
// integers only.

#include <fixy/Core.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;
using ::foundation::core::with_mut;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The wall time of one row, in milliseconds.  A row of 2^24 floats takes
// some milliseconds for each call, so the cap still gives hundreds of
// samples.
constexpr std::size_t kRowWallMs = 2000;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the row, the core of the row, the percentiles
// of one call, and the rate of elements at the median.
void print_row(fixy::TextView name, std::size_t elements, bench::Report const& measured) noexcept {
    double const median = measured.pct.p50 > 0.0 ? measured.pct.p50 : 1.0;
    fixy::report(fixy::Sink::Out,
                 "{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille  rate={} elements/us\n", name,
                 measured.pinned_cpu.raw(), nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(static_cast<double>(elements) * 1000.0 / median + 0.5));
}

// Measures one row with the pin of CRUCIBLE_BENCH_CORE and the wall cap of
// kRowWallMs.
template <typename Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    return run.measure(static_cast<Body&&>(body));
}

// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, from a linear congruential stream: the data of probe E4.
[[nodiscard]] HeapArray<float> draw_floats(std::size_t count) noexcept {
    HeapArray<float> run = ::foundation::core::mint_heap_array(kAlloc, count, 0.0f);
    std::uint64_t state = 0x9E3779B97F4A7C15u;
    for (float& value : run) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        auto const bits = static_cast<std::uint32_t>(state >> 32);
        std::uint32_t const exponent = 127u + (bits >> 23) % 24u - 12u;
        value = ::fixy::bit_cast<float>((bits & 0x80000000u) | (exponent << 23) | (bits & 0x7FFFFFu));
    }
    return run;
}

[[nodiscard]] View<float const> first(View<float const> all, std::size_t count) noexcept {
    return all.window(0, count).expect("the part fits the run");
}

// The left fold of the run: one chain of adds.
[[nodiscard]] float left_fold(View<float const> run) noexcept {
    float sum = 0.0f;
    for (float const value : run) {
        sum += value;
    }
    return sum;
}

void bench_reduces(View<float const> all, View<double const> wide) {
    View<float const> const small = first(all, 1024);
    View<float const> const core = first(all, 65536);
    View<float const> const shared = first(all, std::size_t{1} << 20);
    View<float const> const memory = first(all, std::size_t{1} << 24);
    print_row("sum_by_tree float, 1024", small.size(), measure_row(bench::Run{"sum_by_tree 1024"}, [&] {
                  bench::do_not_optimize(::foundation::core::sum_by_tree(small));
              }));
    print_row("sum_by_tree float, 65536", core.size(), measure_row(bench::Run{"sum_by_tree 65536"}, [&] {
                  bench::do_not_optimize(::foundation::core::sum_by_tree(core));
              }));
    print_row("sum_by_tree float, 1048576", shared.size(), measure_row(bench::Run{"sum_by_tree 2^20"}, [&] {
                  bench::do_not_optimize(::foundation::core::sum_by_tree(shared));
              }));
    print_row("sum_by_tree float, 16777216", memory.size(), measure_row(bench::Run{"sum_by_tree 2^24"}, [&] {
                  bench::do_not_optimize(::foundation::core::sum_by_tree(memory));
              }));
    print_row("left fold float, 65536", core.size(),
              measure_row(bench::Run{"left fold 65536"}, [&] { bench::do_not_optimize(left_fold(core)); }));
    print_row("sum_by_tree double, 65536", wide.size(), measure_row(bench::Run{"sum_by_tree double 65536"}, [&] {
                  bench::do_not_optimize(::foundation::core::sum_by_tree(wide));
              }));
    auto const add = [](float const& left, float const& right) noexcept { return left + right; };
    print_row("reduce_by_tree float add, 65536", core.size(), measure_row(bench::Run{"reduce_by_tree 65536"}, [&] {
                  bench::do_not_optimize(::foundation::core::reduce_by_tree(core, add).value_or(0.0f));
              }));
}

void bench_runs_and_segments(View<float const> all) {
    constexpr std::size_t kCount = 65536;
    ::fixy::FixedArray<View<float const>, 8> runs{};
    std::size_t offset = 0;
    for (View<float const>& run : runs) {
        run = all.window(offset, kCount).expect("the run fits");
        offset += kCount;
    }
    HeapArray<float> sums = ::foundation::core::mint_heap_array(kAlloc, kCount, 0.0f);
    View<View<float const> const> const run_view{runs.view()};
    print_row("sum_runs_by_tree float, 8 runs of 65536", 8 * kCount,
              measure_row(bench::Run{"sum_runs_by_tree 8x65536"}, [&] {
                  sums = with_mut(std::move(sums), [&](auto out) noexcept {
                      bench::do_not_optimize(::foundation::core::sum_runs_by_tree(run_view, std::move(out)).is_ok());
                  });
              }));
    HeapArray<std::size_t> offsets = ::foundation::core::mint_heap_array(kAlloc, std::size_t{257}, std::size_t{0});
    std::size_t start = 0;
    for (std::size_t& segment_offset : offsets) {
        segment_offset = start;
        start += 256;
    }
    HeapArray<float> segment_sums = ::foundation::core::mint_heap_array(kAlloc, std::size_t{256}, 0.0f);
    View<float const> const segmented = first(all, kCount);
    View<std::size_t const> const offset_view{offsets.view()};
    print_row("sum_segments_by_tree float, 256 segments of 256", kCount,
              measure_row(bench::Run{"sum_segments_by_tree 256x256"}, [&] {
                  segment_sums = with_mut(std::move(segment_sums), [&](auto out) noexcept {
                      bench::do_not_optimize(
                          ::foundation::core::sum_segments_by_tree(segmented, offset_view, std::move(out)).is_ok());
                  });
              }));
}

void bench_scans(View<float const> all) {
    View<float const> const core = first(all, 65536);
    View<float const> const shared = first(all, std::size_t{1} << 20);
    HeapArray<float> out = ::foundation::core::mint_heap_array(kAlloc, shared.size(), 0.0f);
    HeapArray<float> core_out = ::foundation::core::mint_heap_array(kAlloc, core.size(), 0.0f);
    print_row("scan_inclusive_sums float, 65536", core.size(), measure_row(bench::Run{"inclusive scan 65536"}, [&] {
                  core_out = with_mut(std::move(core_out), [&](auto part) noexcept {
                      bench::do_not_optimize(::foundation::core::scan_inclusive_sums(core, std::move(part)).is_ok());
                  });
              }));
    print_row("scan_exclusive_sums float, 65536", core.size(), measure_row(bench::Run{"exclusive scan 65536"}, [&] {
                  core_out = with_mut(std::move(core_out), [&](auto part) noexcept {
                      bench::do_not_optimize(::foundation::core::scan_exclusive_sums(core, std::move(part)).is_ok());
                  });
              }));
    print_row("scan_inclusive_sums float, 1048576", shared.size(), measure_row(bench::Run{"inclusive scan 2^20"}, [&] {
                  out = with_mut(std::move(out), [&](auto whole) noexcept {
                      bench::do_not_optimize(::foundation::core::scan_inclusive_sums(shared, std::move(whole)).is_ok());
                  });
              }));
}

// The report of one sort by the network of N, of a copy of the first N
// elements.
template <std::size_t N>
[[nodiscard]] bench::Report measure_network(bench::Run&& run, View<float const> all) {
    ::fixy::FixedArray<float, N> source{};
    std::size_t position = 0;
    for (float& value : source) {
        value = *all.get(position).expect("the element fits the run");
        ++position;
    }
    ::fixy::FixedArray<float, N> work{};
    return measure_row(static_cast<bench::Run&&>(run), [&] {
        work = source;
        work =
            with_mut(std::move(work), [](auto out) noexcept { ::foundation::core::sort_by_network(std::move(out)); });
        bench::do_not_optimize(work);
    });
}

// The stable sort of work with the scratch of scratch, through a frame of
// each, by the order of the caller or by Ascending.  True when the sort gave
// no error.
template <class T, class... Less>
[[nodiscard]] bool sort_through_frames(HeapArray<T>& work, HeapArray<T>& scratch, Less... less) noexcept {
    bool is_sorted = false;
    work = with_mut(std::move(work), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(elements), std::move(room), less...).is_ok();
        });
    });
    return is_sorted;
}

// The report of one stable sort of a copy of part, in a work buffer of its
// count.
template <class T, class... Less>
[[nodiscard]] bench::Report measure_sort_stably(bench::Run&& run, View<T const> part, Less... less) {
    HeapArray<T> work = ::foundation::core::mint_heap_array(kAlloc, part.size(), T{});
    HeapArray<T> scratch = ::foundation::core::mint_heap_array(kAlloc, part.size(), T{});
    return measure_row(static_cast<bench::Run&&>(run), [&] {
        static_cast<void>(::fixy::copy(work.view(), part));
        bench::do_not_optimize(sort_through_frames(work, scratch, less...));
    });
}

// The order of Ascending on a float, as a function of the caller, so that a
// sort takes the path of a comparator of the caller.
struct FloatTotalOrder {
    [[nodiscard]] bool operator()(float const& left, float const& right) const noexcept {
        return ::foundation::core::encode_total_order_key(left) < ::foundation::core::encode_total_order_key(right);
    }
};

// The words of the float bits of all, as keys of an integer sort.
[[nodiscard]] HeapArray<std::uint32_t> words_of(View<float const> all) noexcept {
    HeapArray<std::uint32_t> words = ::foundation::core::mint_heap_array(kAlloc, all.size(), std::uint32_t{0});
    std::size_t position = 0;
    for (std::uint32_t& word : words) {
        word = ::fixy::bit_cast<std::uint32_t>(*all.get(position).expect("the element fits the run"));
        ++position;
    }
    return words;
}

// The words of 64 bits of the float bits of all, two floats for each word,
// as keys of eight bytes.
[[nodiscard]] HeapArray<std::uint64_t> wide_words_of(View<float const> all) noexcept {
    HeapArray<std::uint64_t> words = ::foundation::core::mint_heap_array(kAlloc, all.size() / 2, std::uint64_t{0});
    std::size_t position = 0;
    for (std::uint64_t& word : words) {
        std::uint64_t const low = ::fixy::bit_cast<std::uint32_t>(*all.get(position).expect("the element fits"));
        std::uint64_t const high = ::fixy::bit_cast<std::uint32_t>(*all.get(position + 1).expect("the element fits"));
        word = (high << 32) | low;
        position += 2;
    }
    return words;
}

void bench_sorts(View<float const> all) {
    print_row("sort_by_network<16> float", 16, measure_network<16>(bench::Run{"sort_by_network 16"}, all));
    print_row("sort_by_network<64> float", 64, measure_network<64>(bench::Run{"sort_by_network 64"}, all));
    print_row("sort_by_network<256> float", 256, measure_network<256>(bench::Run{"sort_by_network 256"}, all));
    print_row("sort_stably float, 256", 256, measure_sort_stably(bench::Run{"sort_stably 256"}, first(all, 256)));
    print_row("sort_stably float by an order of the caller, 256", 256,
              measure_sort_stably(bench::Run{"sort_stably order 256"}, first(all, 256), FloatTotalOrder{}));
    print_row("sort_stably float, 1024", 1024, measure_sort_stably(bench::Run{"sort_stably 1024"}, first(all, 1024)));
    print_row("sort_stably float by an order of the caller, 1024", 1024,
              measure_sort_stably(bench::Run{"sort_stably order 1024"}, first(all, 1024), FloatTotalOrder{}));
    print_row("sort_stably float, 65536", 65536,
              measure_sort_stably(bench::Run{"sort_stably 65536"}, first(all, 65536)));
    print_row("sort_stably float, 1048576", std::size_t{1} << 20,
              measure_sort_stably(bench::Run{"sort_stably 2^20"}, first(all, std::size_t{1} << 20)));
    HeapArray<std::uint32_t> const words = words_of(first(all, std::size_t{1} << 20));
    print_row("sort_stably uint32, 1048576", std::size_t{1} << 20,
              measure_sort_stably(bench::Run{"sort_stably uint32 2^20"}, View<std::uint32_t const>{words.view()}));
    HeapArray<std::uint64_t> const wide_words = wide_words_of(first(all, std::size_t{1} << 21));
    print_row("sort_stably uint64, 1048576", std::size_t{1} << 20,
              measure_sort_stably(bench::Run{"sort_stably uint64 2^20"}, View<std::uint64_t const>{wide_words.view()}));
    print_row("sort_stably float by an order of the caller, 65536", 65536,
              measure_sort_stably(bench::Run{"sort_stably order 65536"}, first(all, 65536), FloatTotalOrder{}));
    print_row(
        "sort_stably float by an order of the caller, 1048576", std::size_t{1} << 20,
        measure_sort_stably(bench::Run{"sort_stably order 2^20"}, first(all, std::size_t{1} << 20), FloatTotalOrder{}));
    View<float const> const part = first(all, std::size_t{1} << 20);
    HeapArray<float> sorted = ::foundation::core::mint_heap_array_copy<float>(kAlloc, part);
    HeapArray<float> scratch = ::foundation::core::mint_heap_array(kAlloc, part.size(), 0.0f);
    static_cast<void>(sort_through_frames(sorted, scratch));
    View<float const> const sorted_view{sorted.view()};
    print_row("sort_stably float by an order of the caller, 1048576 sorted", std::size_t{1} << 20,
              measure_sort_stably(bench::Run{"sort_stably order sorted 2^20"}, sorted_view, FloatTotalOrder{}));
    std::size_t probe = 0;
    print_row("find_lower_bound float, 1048576 sorted", 1, measure_row(bench::Run{"find_lower_bound 2^20"}, [&] {
                  probe = (probe + 7919) & ((std::size_t{1} << 20) - 1);
                  float const value = *part.get(probe).expect("the probe fits the run");
                  bench::do_not_optimize(::foundation::core::find_lower_bound(sorted_view, value));
              }));
}

}  // namespace

int main() {
    bench::elevate_priority();
    HeapArray<float> const run = draw_floats(std::size_t{1} << 24);
    View<float const> const all{run.view()};
    HeapArray<double> wide = ::foundation::core::mint_heap_array(kAlloc, std::size_t{65536}, 0.0);
    std::size_t position = 0;
    for (double& value : wide) {
        value = static_cast<double>(*all.get(position).expect("the element fits the run"));
        ++position;
    }
    bench_reduces(all, View<double const>{wide.view()});
    bench_runs_and_segments(all);
    bench_scans(all);
    bench_sorts(all);
    return 0;
}
