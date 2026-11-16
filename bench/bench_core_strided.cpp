// Microbench of the walk and the scope of the strided views of the Region
// family (foundation/core/Strided.h) on one thread.
//
// Each row is one pass of an elementwise add, out = left + right, over a
// 64 x 64 matrix of floats.  The runs fit the cache of the core after the
// first pass.  The rows:
//   - the loop of a FixedArray with a proven Index, the code of a raw loop
//   - the walk over three dense views, which merges the two axes and takes
//     the dense loop
//   - the raw loop with a transposed left matrix and with a left matrix of
//     every second column of a wider run, the baselines of the strided rows
//   - the walk with a transposed left view, which takes the strided loop
//   - the walk with a broadcast row as the right view, which takes the dense
//     loop for each row
//   - the walk over a slice of every second column of a wider run
//   - the loop of the indexes of a scope with at() on three parallel views
//
// Each kernel is a function that the compiler does not inline into the
// bench, so its machine code is one symbol that a reader of the binary can
// find.  Each line gives the percentiles of one pass in nanoseconds, and the
// coefficient of variation in thousandths.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

namespace {

constexpr std::size_t kSide = 64;
constexpr std::size_t kElements = kSide * kSide;

using Matrix = ::fixy::FixedArray<float, kElements>;
using Wide = ::fixy::FixedArray<float, 2 * kElements>;
using Square = ::fixy::StridedView<float, 2>;
using SquareRead = ::fixy::StridedView<float const, 2>;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the pass and the percentiles of its samples.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    fixy::report(fixy::Sink::Out, "{}  p50={} ns  p99={} ns  p99.9={} ns  max={} ns  cv={} permille\n", name,
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.p99_9),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// The view of kSide x kSide floats of a run with the two strides.
template <class Element, std::size_t Count>
[[nodiscard]] ::fixy::StridedView<Element, 2> square_of(::fixy::View<Element, Count> run, std::size_t row_stride,
                                                        std::size_t column_stride) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{kSide, kSide};
    ::fixy::FixedArray<std::size_t, 2> const strides{row_stride, column_stride};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the square fits its run");
}

// The kernels.  Each one is out = left + right, and each one is a symbol of
// its own.  Only main calls them, and GCC 16 put the calls of the walk in the
// cold part of main and compiled add_walk for size, with a scalar loop.  A
// kernel of a backend runs on a hot path, so each kernel here is hot.
[[gnu::noinline, gnu::hot]] void add_indexed(Matrix& out, Matrix const& left, Matrix const& right) noexcept {
    for (::fixy::Index<kElements> const index : out.indices()) {
        out.at(index) = left.at(index) + right.at(index);
    }
}

// The raw loop of the same add with a left matrix of two strides, in the
// row-major order of the walk: the baseline of the strided rows.  The walk
// reads its strides at run time, so the baseline does too: noipa stops a
// clone of the function for the constant strides of a call.  The caller
// gives a View of the left run that holds each element that the strides
// reach.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw loop over two strides is the baseline of the strided walk")
[[gnu::noipa, gnu::hot]] void add_raw_strided(Matrix& out, ::fixy::View<float const> left_run, Matrix const& right,
                                              std::size_t row_stride, std::size_t column_stride) noexcept {
    float* const target = &out.at(::fixy::index<0>());
    float const* const left = &*left_run.begin();
    float const* const second = &right.at(::fixy::index<0>());
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            target[row * kSide + column] =
                left[row * row_stride + column * column_stride] + second[row * kSide + column];
        }
    }
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

[[gnu::noinline, gnu::hot]] void add_walk(Square out, SquareRead left, SquareRead right) noexcept {
    bool const is_walked =
        ::fixy::walk([](float& target, float const& first, float const& second) noexcept { target = first + second; },
                     out, left, right)
            .is_ok();
    if (!is_walked) ::fixy::fatal("the three views of the bench have one shape");
}

// The count of the adds, so the bench reads a result of the scope.
[[gnu::noinline, gnu::hot]] std::size_t add_scope(Square out, SquareRead left, SquareRead right) noexcept {
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

}  // namespace

int main() {
    bench::elevate_priority();

    Matrix left{};
    Matrix right{};
    Matrix out{};
    Matrix row{};
    Wide wide{};
    float value = 0.5f;
    for (::fixy::Index<kElements> const index : left.indices()) {
        value = value * 1.25f + 0.125f;
        if (value > 1000.0f) value = 0.5f;
        left.at(index) = value;
        right.at(index) = value * 0.5f;
        row.at(index) = value * 0.25f;
    }
    for (float& element : wide) {
        element = 1.5f;
    }

    Square const out_view = square_of(out.view(), kSide, 1);
    SquareRead const left_view = square_of(::fixy::View<float const, kElements>{left.view()}, kSide, 1);
    SquareRead const right_view = square_of(::fixy::View<float const, kElements>{right.view()}, kSide, 1);
    SquareRead const transposed = left_view.transpose(::fixy::index<0>(), ::fixy::index<1>());
    ::fixy::FixedArray<std::size_t, 1> const row_size{kSide};
    ::fixy::FixedArray<std::size_t, 1> const row_stride{1};
    ::fixy::FixedArray<std::size_t, 2> const square_sizes{kSide, kSide};
    SquareRead const broadcast = ::fixy::mint_strided_view(::fixy::View<float const, kElements>{row.view()},
                                                           row_size.view(), row_stride.view(), 0)
                                     .expect("one row fits")
                                     .broadcast_to(square_sizes.view())
                                     .expect("a row broadcasts to a square");
    SquareRead const every_second = square_of(::fixy::View<float const, 2 * kElements>{wide.view()}, 2 * kSide, 2);

    print_row("FixedArray Index loop, 4096 adds", bench::run("FixedArray Index loop", [&] {
                  add_indexed(out, left, right);
                  bench::do_not_optimize(out);
              }));
    print_row("walk dense, 4096 adds", bench::run("walk dense", [&] {
                  add_walk(out_view, left_view, right_view);
                  bench::do_not_optimize(out);
              }));
    print_row("raw loop transposed left, 4096 adds", bench::run("raw loop transposed left", [&] {
                  add_raw_strided(out, ::fixy::View<float const>{left.view()}, right, 1, kSide);
                  bench::do_not_optimize(out);
              }));
    print_row("raw loop every second column, 4096 adds", bench::run("raw loop every second column", [&] {
                  add_raw_strided(out, ::fixy::View<float const>{wide.view()}, right, 2 * kSide, 2);
                  bench::do_not_optimize(out);
              }));
    print_row("walk transposed left, 4096 adds", bench::run("walk transposed left", [&] {
                  add_walk(out_view, transposed, right_view);
                  bench::do_not_optimize(out);
              }));
    print_row("walk broadcast right row, 4096 adds", bench::run("walk broadcast right row", [&] {
                  add_walk(out_view, left_view, broadcast);
                  bench::do_not_optimize(out);
              }));
    print_row("walk every second column, 4096 adds", bench::run("walk every second column", [&] {
                  add_walk(out_view, every_second, right_view);
                  bench::do_not_optimize(out);
              }));
    print_row("scope indices and at, 4096 adds", bench::run("scope indices and at", [&] {
                  std::size_t const adds = add_scope(out_view, left_view, right_view);
                  bench::do_not_optimize(adds);
                  bench::do_not_optimize(out);
              }));
    return 0;
}
