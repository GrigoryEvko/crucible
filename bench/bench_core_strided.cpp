// Roofline bench of the walks of the strided views of the Region family
// (foundation/core/Strided.h) on one thread.
//
// Each row is one pass of an elementwise kernel over a matrix of floats of R
// rows and C columns.  The bench runs each row at four sizes: the three views
// of an add fit L1, L2 or L3 of one core, or they come from memory.
//   L1      32 x 64       2048 elements,  24 KB for three views
//   L2      256 x 256     65536 elements, 768 KB
//   L3      1024 x 512    524288 elements, 6 MB
//   memory  4096 x 4096   16777216 elements, 192 MB
//
// The rows of each size:
//   raw add, restrict   the raw loop of out = left + right with restrict
//                       pointers: the roof of the size
//   raw add             the same loop with plain pointers, the code of an
//                       index loop: the compiler guards it with an alias check
//   raw transposed, raw step 2
//                       the raw loop with a left matrix of two run-time
//                       strides: the baselines of the strided rows
//   walk dense          the walk over three dense views
//   walk broadcast      the right view is one row with a stride of zero
//   walk step 2         the left view is every second column of a wider run
//   walk transposed     the left view is the transpose of a matrix
//   walk three inputs   out = a * b + c over four dense views
//   scope dense         the loop of the indexes of a scope with at() on three
//                       dense parallel views
//   scope transposed    the same loop with a transposed left view
//   pointwise dense, broadcast, step 2, step 3, transposed, three inputs
//                       write_pointwise of foundation/core/Pointwise.h over
//                       the same views, in one frame of the output.  Step 3
//                       takes the General loop, and the transposed left view
//                       takes the tile path
// The L1 size also has the loop of a FixedArray with a proven Index, the
// code of the index loop of the type system.
//
// Each line gives the percentiles of one pass in nanoseconds, the coefficient
// of variation in thousandths, the bandwidth, and the bandwidth as a part of
// the roof of the size.  The bandwidth counts four bytes for each element of
// each view, so a broadcast row and a step of two count the elements that the
// kernel reads, not the bytes of the lines.  A row with a coefficient of
// variation above 50 thousandths is not valid.
//
// Each kernel is a function that the compiler does not inline into the
// bench, so its machine code is one symbol that a reader of the binary can
// find.  Only main calls the kernels.  GCC 16 put such calls in the cold part
// of main and compiled a kernel for size, so each kernel here is hot.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Pointwise.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

using Plane = ::fixy::StridedView<float, 2>;
using PlaneRead = ::fixy::StridedView<float const, 2>;
using Storage = ::fixy::HeapArray<float, 64>;

// The side of the FixedArray rows of the L1 size.
constexpr std::size_t kSmallRows = 32;
constexpr std::size_t kSmallColumns = 64;
constexpr std::size_t kSmallCount = kSmallRows * kSmallColumns;
using SmallMatrix = ::fixy::FixedArray<float, kSmallCount>;

// One size of the bench: its name, its shape and the wall time of each row.
struct Level {
    ::fixy::TextView name;
    std::size_t rows = 0;
    std::size_t columns = 0;
    std::size_t wall_ms = 0;
};

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the size, the name of the row, its percentiles, its
// bandwidth for bytes in one pass, and that bandwidth as a part of roof, in
// GB/s.  A roof of zero prints no part.
void print_row(Level const& level, ::fixy::TextView name, bench::Report const& measured, std::size_t bytes,
               double roof) noexcept {
    double const bandwidth = static_cast<double>(bytes) / measured.pct.p50;
    std::uint64_t const tenths = static_cast<std::uint64_t>(bandwidth * 10.0 + 0.5);
    std::uint64_t const percent = roof > 0.0 ? static_cast<std::uint64_t>(bandwidth / roof * 100.0 + 0.5) : 0;
    ::fixy::report(::fixy::Sink::Out,
                   "{} {}x{}  {}  p50={} ns  p99={} ns  max={} ns  cv={} permille  {}.{} GB/s  {} percent of roof\n",
                   level.name, level.rows, level.columns, name, nanoseconds(measured.pct.p50),
                   nanoseconds(measured.pct.p99), nanoseconds(measured.pct.max),
                   static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5), tenths / 10, tenths % 10, percent);
}

// The bandwidth of a row in GB/s: bytes in one pass over its median.
[[nodiscard]] double bandwidth_of(bench::Report const& measured, std::size_t bytes) noexcept {
    return static_cast<double>(bytes) / measured.pct.p50;
}

// Measures body on the core of CRUCIBLE_BENCH_CORE for at most wall_ms.
template <class Body>
[[nodiscard]] bench::Report measure(bench::Run run, std::size_t wall_ms, Body&& body) {
    return run.core(bench::env_core()).max_wall_ms(wall_ms).measure(body);
}

// The view of rows x columns elements of a run with the two strides.
template <class Element>
[[nodiscard]] ::fixy::StridedView<Element, 2> plane_of(::fixy::View<Element> run, std::size_t rows, std::size_t columns,
                                                       std::size_t row_stride, std::size_t column_stride) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{rows, columns};
    ::fixy::FixedArray<std::size_t, 2> const strides{row_stride, column_stride};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the plane fits its run");
}

// The raw loops: the roof and the baselines.  noipa stops a clone of a
// function for the constant sizes and strides of a call, because the walks
// read them at run time too.  The caller gives Views that hold each element
// that the strides reach.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw loops are the roof and the baselines of the strided walks")
[[gnu::always_inline]] inline void add_restrict(float* __restrict target, float const* __restrict left,
                                                float const* __restrict right, std::size_t count) noexcept {
    for (std::size_t position = 0; position < count; ++position) {
        target[position] = left[position] + right[position];
    }
}

[[gnu::noipa, gnu::hot]] void add_raw_restrict(::fixy::View<float> out, ::fixy::View<float const> left,
                                               ::fixy::View<float const> right) noexcept {
    add_restrict(&*out.begin(), &*left.begin(), &*right.begin(), out.size());
}

[[gnu::noipa, gnu::hot]] void add_raw(::fixy::View<float> out, ::fixy::View<float const> left,
                                      ::fixy::View<float const> right) noexcept {
    float* const target = &*out.begin();
    float const* const first = &*left.begin();
    float const* const second = &*right.begin();
    for (std::size_t position = 0; position < out.size(); ++position) {
        target[position] = first[position] + second[position];
    }
}

[[gnu::noipa, gnu::hot]] void add_raw_strided(::fixy::View<float> out, ::fixy::View<float const> left_run,
                                              ::fixy::View<float const> right, std::size_t rows, std::size_t columns,
                                              std::size_t row_stride, std::size_t column_stride) noexcept {
    float* const target = &*out.begin();
    float const* const left = &*left_run.begin();
    float const* const second = &*right.begin();
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            target[row * columns + column] =
                left[row * row_stride + column * column_stride] + second[row * columns + column];
        }
    }
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

[[gnu::noinline, gnu::hot]] void add_indexed(SmallMatrix& out, SmallMatrix const& left,
                                             SmallMatrix const& right) noexcept {
    for (::fixy::Index<kSmallCount> const index : out.indices()) {
        out.at(index) = left.at(index) + right.at(index);
    }
}

[[gnu::noinline, gnu::hot]] void walk_add(Plane out, PlaneRead left, PlaneRead right) noexcept {
    bool const is_walked =
        ::fixy::walk([](float& target, float const& first, float const& second) noexcept { target = first + second; },
                     out, left, right)
            .is_ok();
    if (!is_walked) ::fixy::fatal("the three views of the bench have one shape");
}

[[gnu::noinline, gnu::hot]] void walk_three(Plane out, PlaneRead first, PlaneRead second, PlaneRead third) noexcept {
    bool const is_walked =
        ::fixy::walk([](float& target, float const& a, float const& b, float const& c) noexcept { target = a * b + c; },
                     out, first, second, third)
            .is_ok();
    if (!is_walked) ::fixy::fatal("the four views of the bench have one shape");
}

// The count of the adds, so the bench reads a result of the scope.
[[gnu::noinline, gnu::hot]] std::size_t scope_add(Plane out, PlaneRead left, PlaneRead right) noexcept {
    return ::fixy::mint_strided_scope(left, [out, right](auto first) noexcept {
        auto const second = first.parallel(right).expect("the views of the bench have one shape");
        auto const target = first.parallel(out).expect("the views of the bench have one shape");
        std::size_t adds = 0;
        for (auto const index : first.indices()) {
            target.at(index) = first.at(index) + second.at(index);
            ++adds;
        }
        return adds;
    });
}

// The ops of the pointwise rows.  Each one is a generic call, so it is a lane
// op of 16 floats.
struct AddOp final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return left + right;
    }
};
struct MultiplyAddOp final {
    template <class X>
    constexpr X operator()(X first, X second, X third) const noexcept {
        return first * second + third;
    }
};

// The pointwise kernels.  Out is the unique view of a frame, so each kernel is
// a template, and each instance is one hot symbol.
template <class Out>
[[gnu::noinline, gnu::hot]] void pointwise_add(Out& out, PlaneRead left, PlaneRead right) noexcept {
    if (::foundation::core::write_pointwise(out, AddOp{}, left, right).is_err()) {
        ::fixy::fatal("the three views of the bench have one shape");
    }
}

template <class Out>
[[gnu::noinline, gnu::hot]] void pointwise_three(Out& out, PlaneRead first, PlaneRead second,
                                                 PlaneRead third) noexcept {
    if (::foundation::core::write_pointwise(out, MultiplyAddOp{}, first, second, third).is_err()) {
        ::fixy::fatal("the four views of the bench have one shape");
    }
}

// The value of the element at position of an input.
[[nodiscard]] float value_at(std::size_t position) noexcept {
    return static_cast<float>(position % 1021) * 0.25f + 0.5f;
}

// The rows of one size.
void run_level(Level const& level) noexcept {
    auto const test = ::foundation::effects::testing::test();
    std::size_t const rows = level.rows;
    std::size_t const columns = level.columns;
    std::size_t const count = rows * columns;
    auto const fill = [](std::size_t position) noexcept { return value_at(position); };
    Storage out = ::fixy::mint_heap_array_with<float, 64>(test.alloc, count, fill);
    Storage const left = ::fixy::mint_heap_array_with<float, 64>(test.alloc, count, fill);
    Storage const right = ::fixy::mint_heap_array_with<float, 64>(test.alloc, count, fill);
    Storage const third = ::fixy::mint_heap_array_with<float, 64>(test.alloc, count, fill);
    Storage const row = ::fixy::mint_heap_array_with<float, 64>(test.alloc, columns, fill);
    Storage const wide = ::fixy::mint_heap_array_with<float, 64>(test.alloc, 2 * count, fill);
    Storage const wider = ::fixy::mint_heap_array_with<float, 64>(test.alloc, 3 * count, fill);

    ::fixy::View<float const> const left_run = left.view();
    ::fixy::View<float const> const right_run = right.view();
    ::fixy::View<float const> const wide_run = wide.view();
    Plane const out_view = plane_of(out.view(), rows, columns, columns, 1);
    PlaneRead const left_view = plane_of(left_run, rows, columns, columns, 1);
    PlaneRead const right_view = plane_of(right_run, rows, columns, columns, 1);
    PlaneRead const third_view = plane_of(::fixy::View<float const>{third.view()}, rows, columns, columns, 1);
    // The left run read as a matrix of columns x rows, transposed.
    PlaneRead const transposed = plane_of(left_run, rows, columns, 1, rows);
    PlaneRead const every_second = plane_of(wide_run, rows, columns, 2 * columns, 2);
    PlaneRead const every_third = plane_of(::fixy::View<float const>{wider.view()}, rows, columns, 3 * columns, 3);
    ::fixy::FixedArray<std::size_t, 1> const row_size{columns};
    ::fixy::FixedArray<std::size_t, 1> const row_stride{1};
    ::fixy::FixedArray<std::size_t, 2> const plane_sizes{rows, columns};
    ::fixy::FixedArray<std::size_t, 2> const plane_strides{columns, 1};
    PlaneRead const broadcast =
        ::fixy::mint_strided_view(::fixy::View<float const>{row.view()}, row_size.view(), row_stride.view(), 0)
            .expect("one row fits")
            .broadcast_to(plane_sizes.view())
            .expect("a row broadcasts to the plane");

    std::size_t const three_views = 3 * 4 * count;
    std::size_t const four_views = 4 * 4 * count;
    std::size_t const wall = level.wall_ms;

    bench::Report const roof_report = measure(bench::Run{"raw add, restrict"}, wall, [&] {
        add_raw_restrict(out.view(), left_run, right_run);
        bench::do_not_optimize(out);
    });
    double const roof = bandwidth_of(roof_report, three_views);
    print_row(level, "raw add, restrict", roof_report, three_views, roof);
    print_row(level, "raw add",
              measure(bench::Run{"raw add"}, wall,
                      [&] {
                          add_raw(out.view(), left_run, right_run);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "raw transposed",
              measure(bench::Run{"raw transposed"}, wall,
                      [&] {
                          add_raw_strided(out.view(), left_run, right_run, rows, columns, 1, rows);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "raw step 2",
              measure(bench::Run{"raw step 2"}, wall,
                      [&] {
                          add_raw_strided(out.view(), wide_run, right_run, rows, columns, 2 * columns, 2);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "walk dense",
              measure(bench::Run{"walk dense"}, wall,
                      [&] {
                          walk_add(out_view, left_view, right_view);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "walk broadcast",
              measure(bench::Run{"walk broadcast"}, wall,
                      [&] {
                          walk_add(out_view, left_view, broadcast);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "walk step 2",
              measure(bench::Run{"walk step 2"}, wall,
                      [&] {
                          walk_add(out_view, every_second, right_view);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "walk transposed",
              measure(bench::Run{"walk transposed"}, wall,
                      [&] {
                          walk_add(out_view, transposed, right_view);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "walk three inputs",
              measure(bench::Run{"walk three inputs"}, wall,
                      [&] {
                          walk_three(out_view, left_view, right_view, third_view);
                          bench::do_not_optimize(out);
                      }),
              four_views, roof);
    print_row(level, "scope dense",
              measure(bench::Run{"scope dense"}, wall,
                      [&] {
                          std::size_t const adds = scope_add(out_view, left_view, right_view);
                          bench::do_not_optimize(adds);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);
    print_row(level, "scope transposed",
              measure(bench::Run{"scope transposed"}, wall,
                      [&] {
                          std::size_t const adds = scope_add(out_view, transposed, right_view);
                          bench::do_not_optimize(adds);
                          bench::do_not_optimize(out);
                      }),
              three_views, roof);

    // The pointwise rows, in one frame of the output.  The inputs are plain
    // views of other owners, so each walk compares the address ranges.
    out = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), plane_sizes.view(), plane_strides.view(), 0)
                          .expect("a dense plane has no overlap");
        print_row(level, "pointwise dense",
                  measure(bench::Run{"pointwise dense"}, wall, [&] { pointwise_add(unique, left_view, right_view); }),
                  three_views, roof);
        print_row(
            level, "pointwise broadcast",
            measure(bench::Run{"pointwise broadcast"}, wall, [&] { pointwise_add(unique, left_view, broadcast); }),
            three_views, roof);
        print_row(
            level, "pointwise step 2",
            measure(bench::Run{"pointwise step 2"}, wall, [&] { pointwise_add(unique, every_second, right_view); }),
            three_views, roof);
        print_row(
            level, "pointwise step 3",
            measure(bench::Run{"pointwise step 3"}, wall, [&] { pointwise_add(unique, every_third, right_view); }),
            three_views, roof);
        print_row(
            level, "pointwise transposed",
            measure(bench::Run{"pointwise transposed"}, wall, [&] { pointwise_add(unique, transposed, right_view); }),
            three_views, roof);
        print_row(level, "pointwise three inputs",
                  measure(bench::Run{"pointwise three inputs"}, wall,
                          [&] { pointwise_three(unique, left_view, right_view, third_view); }),
                  four_views, roof);
    });
}

// The FixedArray row of the L1 size.
void run_small_index_loop(Level const& level) noexcept {
    SmallMatrix left{};
    SmallMatrix right{};
    SmallMatrix out{};
    for (::fixy::Index<kSmallCount> const index : left.indices()) {
        left.at(index) = value_at(index.raw());
        right.at(index) = value_at(index.raw() + 7);
    }
    print_row(level, "FixedArray Index loop",
              measure(bench::Run{"FixedArray Index loop"}, level.wall_ms,
                      [&] {
                          add_indexed(out, left, right);
                          bench::do_not_optimize(out);
                      }),
              3 * 4 * kSmallCount, 0.0);
}

}  // namespace

int main() {
    bench::elevate_priority();
    ::fixy::FixedArray<Level, 4> const levels{
        Level{.name = "L1", .rows = kSmallRows, .columns = kSmallColumns, .wall_ms = 400},
        Level{.name = "L2", .rows = 256, .columns = 256, .wall_ms = 400},
        Level{.name = "L3", .rows = 1024, .columns = 512, .wall_ms = 800},
        Level{.name = "memory", .rows = 4096, .columns = 4096, .wall_ms = 2500},
    };
    run_small_index_loop(*levels.get(0).expect("four levels"));
    for (Level const& level : levels) {
        run_level(level);
    }
    return 0;
}
