// The regression budget of the walks of the strided views.
//
// Each case runs one pass of an elementwise kernel over a matrix of 256 x
// 256 floats, whose three views fit L2 of one core.  The test divides the
// median time of the case by the median time of the roof of the same run.
// The roof is write_each of foundation/core/Region.h over the ViewMuts of
// three frames, which reads through restrict pointers.  A ratio of one run
// does not change with the clock of the host.  A case past its budget fails
// the test.  A case whose run drifts by more than 5% between its first and
// its second half gives SKIP and passes, as the gate of bench_record_leaf
// does, so a loaded host reports and does not flap.  bench/CMakeLists.txt
// runs the test only in a Release build.
//
// The budgets are the ratios of the bench host of the tier, with a margin
// for the noise of a shared host.  A slower inner loop or a lost tile path
// moves a ratio past its budget.

#include <fixy/Core.h>
#include <foundation/core/Pointwise.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

constexpr std::size_t kRows = 256;
constexpr std::size_t kColumns = 256;
constexpr std::size_t kCount = kRows * kColumns;
// The wall time of each case, in milliseconds.
constexpr std::size_t kWallMs = 600;
// The largest drift of a case that the test judges, in percent.
constexpr double kMaxDrift = 5.0;

using PlaneRead = ::fixy::StridedView<float const, 2>;
using Storage = ::fixy::HeapArray<float, 64>;

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

// The budget of one case: its name and the largest ratio of its median time
// to the median time of the roof.
struct Budget {
    ::fixy::TextView name;
    double ratio = 0.0;
};

// The view of the plane of a run with the two strides.
[[nodiscard]] PlaneRead plane_of(::fixy::View<float const> run, std::size_t row_stride,
                                 std::size_t column_stride) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{kRows, kColumns};
    ::fixy::FixedArray<std::size_t, 2> const strides{row_stride, column_stride};
    return ::fixy::mint_strided_view(run, sizes.view(), strides.view(), 0).expect("the plane fits its run");
}

template <class Body>
[[nodiscard]] bench::Report measure(bench::Run run, Body&& body) {
    return run.core(bench::env_core()).max_wall_ms(kWallMs).measure(body);
}

template <class Out, class First, class Second>
[[gnu::noinline, gnu::hot]] void roof_add(Out out, First const& first, Second const& second) noexcept {
    if (::fixy::write_each(std::move(out), AddOp{}, first, second).is_err()) {
        ::fixy::fatal("the three runs of the roof have one count");
    }
}

template <class Out>
[[gnu::noinline, gnu::hot]] void pointwise_add(Out& out, PlaneRead left, PlaneRead right) noexcept {
    if (::foundation::core::write_pointwise(out, AddOp{}, left, right).is_err()) {
        ::fixy::fatal("the three views of the budget have one shape");
    }
}

template <class Out>
[[gnu::noinline, gnu::hot]] void pointwise_three(Out& out, PlaneRead first, PlaneRead second,
                                                 PlaneRead third) noexcept {
    if (::foundation::core::write_pointwise(out, MultiplyAddOp{}, first, second, third).is_err()) {
        ::fixy::fatal("the four views of the budget have one shape");
    }
}

// The walk of Strided.h is a kernel of its own too: inlined into a lambda of
// main, which GCC compiles as cold code, the same walk took three times the
// time of this kernel.
[[gnu::noinline, gnu::hot]] void walk_add(::fixy::StridedView<float, 2> out, PlaneRead left, PlaneRead right) noexcept {
    bool const is_walked =
        ::fixy::walk([](float& target, float const& first, float const& second) noexcept { target = first + second; },
                     out, left, right)
            .is_ok();
    if (!is_walked) ::fixy::fatal("the three views of the budget have one shape");
}

[[gnu::noinline, gnu::hot]] std::size_t scope_add(::fixy::StridedView<float, 2> out, PlaneRead left,
                                                  PlaneRead right) noexcept {
    return ::fixy::mint_strided_scope(left, [out, right](auto first) noexcept {
        auto const second = first.parallel(right).expect("the views of the budget have one shape");
        auto const target = first.parallel(out).expect("the views of the budget have one shape");
        std::size_t adds = 0;
        for (auto const index : first.indices()) {
            target.at(index) = first.at(index) + second.at(index);
            ++adds;
        }
        return adds;
    });
}

// The verdict of one case: PASS, FAIL or SKIP, with its ratio.  Gives true
// when the case fails.
[[nodiscard]] bool judge(Budget const& budget, bench::Report const& measured, double roof_ns) noexcept {
    double const ratio = measured.pct.p50 / roof_ns;
    std::uint64_t const hundredths = static_cast<std::uint64_t>(ratio * 100.0 + 0.5);
    std::uint64_t const limit = static_cast<std::uint64_t>(budget.ratio * 100.0 + 0.5);
    std::uint64_t const drift = static_cast<std::uint64_t>(measured.drift_pct * 10.0 + 0.5);
    // The hundredths below ten take a zero first, so 1.02 does not print as 1.2.
    ::fixy::TextView const ratio_zero = hundredths % 100 < 10 ? ::fixy::TextView{"0"} : ::fixy::TextView{""};
    ::fixy::TextView const limit_zero = limit % 100 < 10 ? ::fixy::TextView{"0"} : ::fixy::TextView{""};
    if (measured.drift_pct > kMaxDrift) {
        ::fixy::report(::fixy::Sink::Out, "SKIP  {}  ratio {}.{}{} of budget {}.{}{}: the run drifted {}.{} percent\n",
                       budget.name, hundredths / 100, ratio_zero, hundredths % 100, limit / 100, limit_zero,
                       limit % 100, drift / 10, drift % 10);
        return false;
    }
    bool const is_over = ratio > budget.ratio;
    ::fixy::TextView const verdict = is_over ? ::fixy::TextView{"FAIL"} : ::fixy::TextView{"PASS"};
    ::fixy::report(::fixy::Sink::Out, "{}  {}  ratio {}.{}{} of budget {}.{}{}, median {} ns\n", verdict, budget.name,
                   hundredths / 100, ratio_zero, hundredths % 100, limit / 100, limit_zero, limit % 100,
                   static_cast<std::uint64_t>(measured.pct.p50 + 0.5));
    return is_over;
}

[[nodiscard]] float value_at(std::size_t position) noexcept {
    return static_cast<float>(position % 1021) * 0.25f + 0.5f;
}

}  // namespace

int main() {
    bench::elevate_priority();
    auto const test = ::foundation::effects::testing::test();
    auto const fill = [](std::size_t position) noexcept { return value_at(position); };
    Storage out = ::fixy::mint_heap_array_with<float, 64>(test.alloc, kCount, fill);
    Storage left = ::fixy::mint_heap_array_with<float, 64>(test.alloc, kCount, fill);
    Storage right = ::fixy::mint_heap_array_with<float, 64>(test.alloc, kCount, fill);
    Storage const third = ::fixy::mint_heap_array_with<float, 64>(test.alloc, kCount, fill);
    Storage const row = ::fixy::mint_heap_array_with<float, 64>(test.alloc, kColumns, fill);
    Storage const wide = ::fixy::mint_heap_array_with<float, 64>(test.alloc, 2 * kCount, fill);

    bench::Report const roof = measure(bench::Run{"roof"}, [&] {
        out = ::fixy::with_mut(std::move(out), [&](auto target) noexcept {
            left = ::fixy::with_mut(std::move(left), [&](auto first) noexcept {
                right = ::fixy::with_mut(std::move(right),
                                         [&](auto second) noexcept { roof_add(std::move(target), first, second); });
            });
        });
    });
    double const roof_ns = roof.pct.p50;
    ::fixy::report(::fixy::Sink::Out, "roof  median {} ns\n", static_cast<std::uint64_t>(roof_ns + 0.5));
    // Each ratio divides by the roof, so a roof that drifts makes each ratio
    // not valid.
    if (roof.drift_pct > kMaxDrift) {
        std::uint64_t const drift = static_cast<std::uint64_t>(roof.drift_pct * 10.0 + 0.5);
        ::fixy::report(::fixy::Sink::Out, "SKIP  each case: the roof drifted {}.{} percent\nverdict: PASS\n",
                       drift / 10, drift % 10);
        return 0;
    }

    ::fixy::View<float const> const left_run = left.view();
    PlaneRead const left_view = plane_of(left_run, kColumns, 1);
    PlaneRead const right_view = plane_of(::fixy::View<float const>{right.view()}, kColumns, 1);
    PlaneRead const third_view = plane_of(::fixy::View<float const>{third.view()}, kColumns, 1);
    PlaneRead const transposed = plane_of(left_run, 1, kRows);
    PlaneRead const every_second = plane_of(::fixy::View<float const>{wide.view()}, 2 * kColumns, 2);
    ::fixy::FixedArray<std::size_t, 1> const row_size{kColumns};
    ::fixy::FixedArray<std::size_t, 1> const row_stride{1};
    ::fixy::FixedArray<std::size_t, 2> const plane_sizes{kRows, kColumns};
    ::fixy::FixedArray<std::size_t, 2> const plane_strides{kColumns, 1};
    PlaneRead const broadcast =
        ::fixy::mint_strided_view(::fixy::View<float const>{row.view()}, row_size.view(), row_stride.view(), 0)
            .expect("one row fits")
            .broadcast_to(plane_sizes.view())
            .expect("a row broadcasts to the plane");

    // The ratios of four runs on this host (EPYC 9754, Zen 4c, a shared VM):
    // dense 1.0 to 1.10, broadcast 0.64 to 0.67, step 2 1.47 to 1.55,
    // transposed 1.27 to 1.31, three inputs 1.41 to 1.55, walk dense 0.99,
    // and each scope 15.2.  The tile path with its registers on the stack gave
    // the transposed add 3.3, and the general loop gave it 7.8.
    ::fixy::FixedArray<Budget, 8> const budgets{
        Budget{.name = "pointwise dense", .ratio = 1.25},        Budget{.name = "pointwise broadcast", .ratio = 0.85},
        Budget{.name = "pointwise step 2", .ratio = 1.80},       Budget{.name = "pointwise transposed", .ratio = 1.80},
        Budget{.name = "pointwise three inputs", .ratio = 1.80}, Budget{.name = "scope dense", .ratio = 18.0},
        Budget{.name = "scope transposed", .ratio = 18.0},       Budget{.name = "walk dense", .ratio = 1.25},
    };
    auto const budget = [&budgets](std::size_t index) noexcept -> Budget const& {
        return *budgets.get(index).expect("eight budgets");
    };
    bool has_failure = false;
    out = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        auto unique = ::fixy::mint_strided_view_mut(std::move(run), plane_sizes.view(), plane_strides.view(), 0)
                          .expect("a dense plane has no overlap");
        has_failure =
            judge(budget(0),
                  measure(bench::Run{"pointwise dense"}, [&] { pointwise_add(unique, left_view, right_view); }),
                  roof_ns)
            || has_failure;
        has_failure =
            judge(budget(1),
                  measure(bench::Run{"pointwise broadcast"}, [&] { pointwise_add(unique, left_view, broadcast); }),
                  roof_ns)
            || has_failure;
        has_failure =
            judge(budget(2),
                  measure(bench::Run{"pointwise step 2"}, [&] { pointwise_add(unique, every_second, right_view); }),
                  roof_ns)
            || has_failure;
        has_failure =
            judge(budget(3),
                  measure(bench::Run{"pointwise transposed"}, [&] { pointwise_add(unique, transposed, right_view); }),
                  roof_ns)
            || has_failure;
        has_failure = judge(budget(4),
                            measure(bench::Run{"pointwise three inputs"},
                                    [&] { pointwise_three(unique, left_view, right_view, third_view); }),
                            roof_ns)
                   || has_failure;
    });
    ::fixy::StridedView<float, 2> const out_view =
        ::fixy::mint_strided_view(out.view(), plane_sizes.view(), plane_strides.view(), 0).expect("the plane fits");
    has_failure = judge(budget(5),
                        measure(bench::Run{"scope dense"},
                                [&] {
                                    std::size_t const adds = scope_add(out_view, left_view, right_view);
                                    bench::do_not_optimize(adds);
                                }),
                        roof_ns)
               || has_failure;
    has_failure = judge(budget(6),
                        measure(bench::Run{"scope transposed"},
                                [&] {
                                    std::size_t const adds = scope_add(out_view, transposed, right_view);
                                    bench::do_not_optimize(adds);
                                }),
                        roof_ns)
               || has_failure;
    has_failure =
        judge(budget(7), measure(bench::Run{"walk dense"}, [&] { walk_add(out_view, left_view, right_view); }), roof_ns)
        || has_failure;
    ::fixy::report(::fixy::Sink::Out, "verdict: {}\n",
                   has_failure ? ::fixy::TextView{"FAIL"} : ::fixy::TextView{"PASS"});
    return has_failure ? 1 : 0;
}
