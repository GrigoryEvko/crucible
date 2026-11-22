// Microbench of the chains of the Walk family (foundation/core/Walk.h) on one
// thread, against the raw loop that each chain replaces and against the C++26
// ranges of libstdc++.
//
// The rows:
//   map + fold, f32       the sum of the squares of 4096 floats, left to right
//   filter + count, u32   the count of the words of 4096 that three divides
//   zip + fold, f32       the dot product of two runs of 4096 floats, left to
//                         right
// Each shape has three rows: the walk, the raw loop over a pointer and a
// count, and the ranges form (views::transform, views::filter and
// views::zip_transform with ranges::fold_left and ranges::distance).  The raw
// loop is the reference of the speed rule: the rule codegen-shapes compares
// the code of each chain with its raw loop (test/shape/core_shapes.cpp), so
// the ratio of the walk to the raw loop is 1 by the code, and this bench
// gives the time.  A float fold is the left order, not the speed of a tree
// sum: sum_by_tree of foundation/core/Reduce.h is the fast road.
//
// Each kernel is a function that the compiler does not inline into the
// bench, so its machine code is one symbol that a reader of the binary can
// find.  Each line gives the percentiles of one pass in nanoseconds, the
// coefficient of variation in thousandths, and the median as a part of the
// median of the raw loop of its shape, in thousandths.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>

#include "bench_harness.h"

namespace {

// The count of the elements of each run.
constexpr std::size_t kElements = 4096;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the row, its percentiles, and its median as a
// part of the median of reference, in thousandths.
void print_row(fixy::TextView name, bench::Report const& measured, bench::Report const& reference) noexcept {
    bench::print("{}  p50={} ns  p99={} ns  max={} ns  cv={} permille  {} permille of raw\n", name,
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.max),
                 static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(measured.pct.p50 / reference.pct.p50 * 1000.0 + 0.5));
}

// ── The walks ───────────────────────────────────────────────────────────────

[[gnu::noinline, gnu::hot]] float walk_map_fold(fixy::View<float const> run) noexcept {
    return run.walk()
        .map([](float const& value) noexcept { return value * value; })
        .fold(0.0f, [](float sum, float square) noexcept { return sum + square; });
}

[[gnu::noinline, gnu::hot]] std::size_t walk_filter_count(fixy::View<std::uint32_t const> run) noexcept {
    return run.walk().filter([](std::uint32_t const& value) noexcept { return value % 3 == 0; }).count();
}

[[gnu::noinline, gnu::hot]] float walk_zip_fold(fixy::View<float const, kElements> left,
                                                fixy::View<float const, kElements> right) noexcept {
    return left.walk()
        .zip(right.walk())
        .map([](auto const& pair) noexcept {
            auto const& [first, second] = pair;
            return *first * *second;
        })
        .fold(0.0f, [](float sum, float product) noexcept { return sum + product; });
}

// ── The raw loops and the ranges ────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw loop and the ranges form are the references of each walk")

// The raw loops read the pointer of the first element of a run of 4096
// elements, so the read through the cursor never meets the end.
[[gnu::noinline, gnu::hot]] float raw_map_fold(fixy::View<float const> run) noexcept {
    float const* const first = &*run.begin();
    std::size_t const count = run.size();
    float sum = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        sum += first[index] * first[index];
    }
    return sum;
}

[[gnu::noinline, gnu::hot]] std::size_t raw_filter_count(fixy::View<std::uint32_t const> run) noexcept {
    std::uint32_t const* const first = &*run.begin();
    std::size_t const count = run.size();
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] % 3 == 0) ++kept;
    }
    return kept;
}

[[gnu::noinline, gnu::hot]] float raw_zip_fold(fixy::View<float const, kElements> left_run,
                                               fixy::View<float const, kElements> right_run) noexcept {
    float const* const left = &*left_run.begin();
    float const* const right = &*right_run.begin();
    float sum = 0.0f;
    for (std::size_t index = 0; index < kElements; ++index) {
        sum += left[index] * right[index];
    }
    return sum;
}

[[gnu::noinline, gnu::hot]] float ranges_map_fold(std::span<float const> run) noexcept {
    return std::ranges::fold_left(run | std::views::transform([](float value) noexcept { return value * value; }), 0.0f,
                                  std::plus<>{});
}

[[gnu::noinline, gnu::hot]] std::size_t ranges_filter_count(std::span<std::uint32_t const> run) noexcept {
    return static_cast<std::size_t>(
        std::ranges::distance(run | std::views::filter([](std::uint32_t value) noexcept { return value % 3 == 0; })));
}

[[gnu::noinline, gnu::hot]] float ranges_zip_fold(std::span<float const, kElements> left,
                                                  std::span<float const, kElements> right) noexcept {
    return std::ranges::fold_left(std::views::zip_transform(std::multiplies<>{}, left, right), 0.0f, std::plus<>{});
}

// The span of the elements of a View, for the ranges rows.
template <class T, std::size_t Extent>
[[nodiscard]] std::span<T const, Extent == fixy::dynamic_extent ? std::dynamic_extent : Extent>
span_of(fixy::View<T const, Extent> run) noexcept {
    T const* const first = &*run.begin();
    if constexpr (Extent == fixy::dynamic_extent) {
        return std::span<T const>{first, run.size()};
    } else {
        return std::span<T const, Extent>{first, Extent};
    }
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    bench::elevate_priority();

    // A fixed pattern of values in two runs of floats and one run of words.
    // The floats are small, so no sum overflows.
    ::foundation::core::FixedArray<float, kElements> left{};
    ::foundation::core::FixedArray<float, kElements> right{};
    ::foundation::core::FixedArray<std::uint32_t, kElements> words{};
    std::uint32_t pattern = 0x9E3779B9u;
    for (fixy::Index<kElements> index : left.indices()) {
        pattern = pattern * 1664525u + 1013904223u;
        words.with_element(index, [pattern](std::uint32_t& element) noexcept { element = pattern; });
        left.with_element(index,
                          [pattern](float& element) noexcept { element = static_cast<float>(pattern >> 24) / 256.0f; });
        right.with_element(
            index, [pattern](float& element) noexcept { element = static_cast<float>(pattern & 0xFFu) / 256.0f; });
    }
    fixy::View<float const> const run = left.view();
    fixy::View<std::uint32_t const> const word_run = words.view();
    fixy::View<float const, kElements> const left_run = left.view();
    fixy::View<float const, kElements> const right_run = right.view();

    // Each body takes its runs through the input sink, so each call computes
    // its result again (the sink of bench_harness.h).  Each function of a row
    // is pure, so with no sink the optimizer calls it one time before the
    // timed loop.
    bench::Report const raw_map =
        bench::run("raw map + fold", [&] { bench::do_not_optimize(raw_map_fold(bench::make_opaque(run))); });
    bench::Report const walk_map =
        bench::run("walk map + fold", [&] { bench::do_not_optimize(walk_map_fold(bench::make_opaque(run))); });
    bench::Report const ranges_map = bench::run(
        "ranges map + fold", [&] { bench::do_not_optimize(ranges_map_fold(span_of(bench::make_opaque(run)))); });
    print_row("map + fold, f32, raw loop", raw_map, raw_map);
    print_row("map + fold, f32, walk", walk_map, raw_map);
    print_row("map + fold, f32, C++ ranges", ranges_map, raw_map);

    bench::Report const raw_filter = bench::run(
        "raw filter + count", [&] { bench::do_not_optimize(raw_filter_count(bench::make_opaque(word_run))); });
    bench::Report const walk_filter = bench::run(
        "walk filter + count", [&] { bench::do_not_optimize(walk_filter_count(bench::make_opaque(word_run))); });
    bench::Report const ranges_filter = bench::run("ranges filter + count", [&] {
        bench::do_not_optimize(ranges_filter_count(span_of(bench::make_opaque(word_run))));
    });
    print_row("filter + count, u32, raw loop", raw_filter, raw_filter);
    print_row("filter + count, u32, walk", walk_filter, raw_filter);
    print_row("filter + count, u32, C++ ranges", ranges_filter, raw_filter);

    bench::Report const raw_zip = bench::run("raw zip + fold", [&] {
        bench::do_not_optimize(raw_zip_fold(bench::make_opaque(left_run), bench::make_opaque(right_run)));
    });
    bench::Report const walk_zip = bench::run("walk zip + fold", [&] {
        bench::do_not_optimize(walk_zip_fold(bench::make_opaque(left_run), bench::make_opaque(right_run)));
    });
    bench::Report const ranges_zip = bench::run("ranges zip + fold", [&] {
        bench::do_not_optimize(
            ranges_zip_fold(span_of(bench::make_opaque(left_run)), span_of(bench::make_opaque(right_run))));
    });
    print_row("zip + fold, f32, raw loop", raw_zip, raw_zip);
    print_row("zip + fold, f32, walk", walk_zip, raw_zip);
    print_row("zip + fold, f32, C++ ranges", ranges_zip, raw_zip);
    return 0;
}
