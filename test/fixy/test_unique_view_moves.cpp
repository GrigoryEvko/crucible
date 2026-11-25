// A run that a ViewMut gave up has no second name
// (the unique view of foundation/core/Region.h).
//
// Two ViewMuts of two brands name two runs, and each door of a kernel trusts
// that proof: it takes the loop of restrict pointers, the memcpy, or no compare
// of the bytes.  A region of the region scope door lends the run of a moved
// ViewMut with a brand of its own.  So an old name of that ViewMut with a run
// would be a second name of the run with another brand.  Each test here gives
// one ViewMut to two parameters, so the rule use-after-move does not see that
// the old name moved, and takes a window of the old name for an input.  The
// old name has no element, so the window does not fit and the process ends
// before the door of the kernel.
//
//   - The copy of one ViewMut into another, write_each, the pointwise walk, the
//     parallel pointwise walk, and the parallel scan.
//   - A sort gets the run and no scratch, so it refuses the two counts.
//   - A plain view of the run proves no difference, so the doors take the
//     stated order over it: each build gives one result for an input that
//     overlaps the output at a shift of one element.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/OwnedRegion.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/ParallelPointwise.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Pointwise.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace core = ::foundation::core;
namespace eff = ::foundation::effects;

using ::fixy::check::expect_dies;
using ::fixy::check::pass;
using ::fixy::spawn::BodyCoupling;

// The context of the parallel forms: it owns Bg for the threads of the shards.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The tag of the regions of the region scope door.
struct RegionWhole {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

struct PlusOne {
    template <class X>
    constexpr X operator()(X const& value) const noexcept {
        return static_cast<X>(value + X{1});
    }
};

constexpr std::size_t kCount = 4097;

// The sum of 1, 2, 3 and so on to kCount: the elements that PlusOne gives over
// a run of ones when its input is the run of the output one element before it.
constexpr std::uint64_t kSumOfSteps = std::uint64_t{kCount} * (kCount + 1) / 2;

constexpr ::fixy::TextView kNoWindow{"the window fits"};

[[nodiscard]] ::fixy::HeapArray<float> ones() noexcept {
    return ::fixy::mint_heap_array(eff::testing::test().alloc, kCount, 1.0f);
}

[[nodiscard]] std::uint64_t sum_of(::fixy::View<float const> run) noexcept {
    std::uint64_t sum = 0;
    for (float const element : run)
        sum += static_cast<std::uint64_t>(element);
    return sum;
}

// Gives the run of lent to a region, and calls kernel with the elements of the
// region from the second one, and with the window of the old name of lent that
// starts at the first one.
template <class Kernel, class Lent>
void run_under_two_names(Kernel kernel, Lent& lent, Lent& old_name) noexcept {
    std::size_t const count = lent.size();
    static_cast<void>(
        ::fixy::with_region<RegionWhole>(std::move(lent), [kernel, &old_name, count](auto region) noexcept {
            return ::fixy::with_mut(std::move(region), [kernel, &old_name, count](auto out) noexcept {
                auto [head, tail] = core::split_at(std::move(out), 1).expect("the head fits");
                auto [window, rest] = core::split_at(std::move(old_name), count - 1).expect("the window fits");
                static_cast<void>(head);
                static_cast<void>(rest);
                kernel(std::move(tail), std::move(window));
            });
        }));
}

// Runs kernel under two names of one run.
template <class Kernel>
void run_kernel(Kernel kernel) noexcept {
    ::fixy::HeapArray<float> items = ones();
    items =
        ::fixy::with_mut(std::move(items), [kernel](auto lent) noexcept { run_under_two_names(kernel, lent, lent); });
}

// The strided view of rank one over a run of kCount - 1 elements.
template <class Run>
[[nodiscard]] auto line_of(Run run) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{kCount - 1};
    ::fixy::FixedArray<std::size_t, 1> const strides{1};
    return ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the line fits");
}

// Each kernel takes two runs of two brands.
struct CopyKernel {
    template <class Out, class In>
    void operator()(Out out, In in) const noexcept {
        core::copy(std::move(out), in).expect("one count");
    }
};
struct WriteEachKernel {
    template <class Out, class In>
    void operator()(Out out, In in) const noexcept {
        core::write_each(std::move(out), PlusOne{}, in).expect("one count");
    }
};
struct PointwiseKernel {
    template <class Out, class In>
    void operator()(Out out, In in) const noexcept {
        auto target = line_of(std::move(out));
        auto const source = line_of(std::move(in));
        core::write_pointwise(target, PlusOne{}, source).expect("one shape");
    }
};
struct ParallelPointwiseKernel {
    template <class Out, class In>
    void operator()(Out out, In in) const noexcept {
        BgCtx const ctx{eff::testing::bg()};
        auto target = line_of(std::move(out));
        auto const source = line_of(std::move(in));
        ::fixy::concurrent::write_pointwise_in_parallel<BodyCoupling::WaitOnEachOther, 2>(ctx, target, PlusOne{},
                                                                                          source)
            .expect("one shape");
    }
};
struct ParallelScanKernel {
    template <class Out, class In>
    void operator()(Out out, In in) const noexcept {
        BgCtx const ctx{eff::testing::bg()};
        ::fixy::concurrent::scan_sums_in_parallel<BodyCoupling::WaitOnEachOther, 4>(
            ctx, eff::testing::test().alloc, core::ScanKind::Inclusive, in, std::move(out));
    }
};

// The old name of a run that a region took gives no window, so no door of a
// kernel takes the run two times.
void test_old_name_gives_no_second_run(::fixy::TestRunnerCtx const& test) {
    expect_dies(test, [] noexcept { run_kernel(CopyKernel{}); }, kNoWindow);
    expect_dies(test, [] noexcept { run_kernel(WriteEachKernel{}); }, kNoWindow);
    expect_dies(test, [] noexcept { run_kernel(PointwiseKernel{}); }, kNoWindow);
    expect_dies(test, [] noexcept { run_kernel(ParallelPointwiseKernel{}); }, kNoWindow);
    expect_dies(test, [] noexcept { run_kernel(ParallelScanKernel{}); }, kNoWindow);
    pass(test, "  test_old_name_gives_no_second_run: PASSED\n");
}

// The old name of a ViewMut has no element while the region lives and after
// it, and the ViewMut that the door gives back has each element.
template <class Lent>
void adopt_under_two_names(::fixy::TestRunnerCtx const& test, Lent& lent, Lent& old_name) noexcept {
    auto const back = ::fixy::with_region<RegionWhole>(std::move(lent), [&test, &old_name](auto region) noexcept {
        CRUCIBLE_TEST_ASSERT(test, old_name.size() == 0 && region.size() == kCount);
        return region;
    });
    CRUCIBLE_TEST_ASSERT(test, old_name.size() == 0 && back.size() == kCount);
}

void test_region_empties_its_run(::fixy::TestRunnerCtx const& test) {
    ::fixy::HeapArray<float> items = ones();
    items =
        ::fixy::with_mut(std::move(items), [&test](auto lent) noexcept { adopt_under_two_names(test, lent, lent); });
    CRUCIBLE_TEST_ASSERT(test, sum_of(items.view()) == kCount);
    pass(test, "  test_region_empties_its_run: PASSED\n");
}

// A sort takes its elements and its scratch as two unique borrows, so they
// share no element.  Two parameters name one ViewMut, so one of the two has the
// run and the other one has no element.
template <class Lent>
void sort_into_the_old_name(::fixy::TestRunnerCtx const& test, Lent& lent, Lent& old_name) noexcept {
    bool const is_refused = core::sort_stably(std::move(lent), std::move(old_name)).is_err();
    CRUCIBLE_TEST_ASSERT(test, is_refused);
}

// The sort compares the two counts before it reads an element, so a refused
// sort moves none.
void test_sort_gets_no_run_two_times(::fixy::TestRunnerCtx const& test) {
    auto const falling = [](std::size_t position) noexcept { return static_cast<float>(kCount - position); };
    ::fixy::HeapArray<float> items = ::fixy::mint_heap_array_with<float>(eff::testing::test().alloc, kCount, falling);
    items =
        ::fixy::with_mut(std::move(items), [&test](auto lent) noexcept { sort_into_the_old_name(test, lent, lent); });
    bool is_each_kept = true;
    std::size_t position = 0;
    for (float const element : items) {
        is_each_kept = is_each_kept && static_cast<std::size_t>(element) == kCount - position;
        ++position;
    }
    CRUCIBLE_TEST_ASSERT(test, is_each_kept && position == kCount);
    pass(test, "  test_sort_gets_no_run_two_times: PASSED\n");
}

// The stated order of write_each and of the pointwise walk over an input that
// is the run of the output one element before it: each position reads what the
// position before it wrote.  The input is a plain view, which has no brand.
void test_overlap_takes_the_stated_order(::fixy::TestRunnerCtx const& test) {
    ::fixy::HeapArray<float> items = ones();
    items = ::fixy::with_mut(std::move(items), [](auto lent) noexcept {
        ::fixy::View<float const> const input = lent.view().window(0, kCount - 1).expect("the input fits");
        auto [head, tail] = core::split_at(std::move(lent), 1).expect("the head fits");
        static_cast<void>(head);
        core::write_each(std::move(tail), PlusOne{}, input).expect("one count");
    });
    CRUCIBLE_TEST_ASSERT(test, sum_of(items.view()) == kSumOfSteps);

    items = ones();
    items = ::fixy::with_mut(std::move(items), [](auto lent) noexcept {
        ::fixy::FixedArray<std::size_t, 1> const sizes{kCount - 1};
        ::fixy::FixedArray<std::size_t, 1> const strides{1};
        auto const source =
            ::fixy::mint_strided_view(::fixy::View<float const>{lent.view()}, sizes.view(), strides.view(), 0)
                .expect("the input fits");
        auto [head, tail] = core::split_at(std::move(lent), 1).expect("the head fits");
        static_cast<void>(head);
        auto target = line_of(std::move(tail));
        core::write_pointwise(target, PlusOne{}, source).expect("one shape");
    });
    CRUCIBLE_TEST_ASSERT(test, sum_of(items.view()) == kSumOfSteps);
    pass(test, "  test_overlap_takes_the_stated_order: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    test_old_name_gives_no_second_run(test);
    test_region_empties_its_run(test);
    test_sort_gets_no_run_two_times(test);
    test_overlap_takes_the_stated_order(test);
    pass(test, "test_unique_view_moves: all tests passed\n");
    return 0;
}
