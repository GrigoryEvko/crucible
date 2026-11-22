// The parallel sum scans of fixy/concurrent/ParallelTree.h, run for real on
// 1, 2, 4, 8 and 64 threads (parallel_tree_test.h).  Each output must have
// the bits of scan_inclusive_sums and scan_exclusive_sums of one thread.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Scan.h>

#include "../foundation/nan_outputs.h"
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "parallel_tree_test.h"

#include <cstddef>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

// The parallel sum scan of kind of part into parallel, and the scan of one
// thread into single, each through a frame of its owner.
template <spawn::BodyCoupling Coupling, std::size_t Shards>
void scan_both(tree::BgCtx const& ctx, ::foundation::core::ScanKind kind, View<float const> part,
               HeapArray<float>& parallel, HeapArray<float>& single) noexcept {
    parallel = ::foundation::core::with_mut(std::move(parallel), [&](auto out) noexcept {
        ::fixy::concurrent::scan_sums_in_parallel<Coupling, Shards>(ctx, tree::test_alloc(), kind, part,
                                                                    std::move(out));
    });
    single = ::foundation::core::with_mut(std::move(single), [&](auto out) noexcept {
        if (kind == ::foundation::core::ScanKind::Inclusive) {
            CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::scan_inclusive_sums(part, std::move(out)).is_ok());
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::scan_exclusive_sums(part, std::move(out)).is_ok());
        }
    });
}

// The parallel sum scans give the bits of the scans of one thread for each
// count.
template <spawn::BodyCoupling Coupling, std::size_t Shards, std::size_t Counts>
void check_scans(View<float const> all, ::fixy::FixedArray<std::size_t, Counts> const& counts) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        HeapArray<float> parallel_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        HeapArray<float> single_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        scan_both<Coupling, Shards>(ctx, ::foundation::core::ScanKind::Inclusive, part, parallel_scan, single_scan);
        CRUCIBLE_TEST_ASSERT(
            kTest, tree::same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
        scan_both<Coupling, Shards>(ctx, ::foundation::core::ScanKind::Exclusive, part, parallel_scan, single_scan);
        CRUCIBLE_TEST_ASSERT(
            kTest, tree::same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
    }
}

void test_scans_on_each_thread_count() {
    HeapArray<float> const run = tree::draw_floats(tree::kLongest, 0x9A4A0005u);
    View<float const> const all{run.view()};
    check_scans<spawn::BodyCoupling::WaitOnEachOther, 1>(all, tree::kCounts);
    check_scans<spawn::BodyCoupling::WaitOnEachOther, 2>(all, tree::kCounts);
    check_scans<spawn::BodyCoupling::WaitOnEachOther, 4>(all, tree::kCounts);
    check_scans<spawn::BodyCoupling::WaitOnEachOther, 8>(all, tree::kCounts);
    check_scans<spawn::BodyCoupling::WaitOnEachOther, 64>(all, tree::kWideCounts);
    check_scans<spawn::BodyCoupling::Independent, 8>(all, tree::kCounts);
}

// Each output of the parallel scan and of the scan of one thread has the
// output bits of the scan of one thread: the canonical NaN for a NaN (THE
// VALUES OUTSIDE THE BOUND of foundation/core/Reduce.h).
void check_canonical_outputs(View<float const> parallel, View<float const> single) noexcept {
    for (auto const [left, right] : parallel.walk().try_zip(single.walk()).expect("the two runs have one count")) {
        CRUCIBLE_TEST_ASSERT(kTest, ::crucible::test::float_bits(*left) == ::crucible::test::output_bits(*right));
        CRUCIBLE_TEST_ASSERT(kTest, ::crucible::test::float_bits(*right) == ::crucible::test::output_bits(*right));
    }
}

// A run with NaNs and infinities gives the canonical NaN at each NaN output
// of both kinds, on each thread count.
template <spawn::BodyCoupling Coupling, std::size_t Shards>
void check_nan_scans(View<float const> all) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    ::fixy::FixedArray<::foundation::core::ScanKind, 2> const kinds{::foundation::core::ScanKind::Inclusive,
                                                                    ::foundation::core::ScanKind::Exclusive};
    for (std::size_t const count : tree::kCounts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        HeapArray<float> parallel_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        HeapArray<float> single_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        for (::foundation::core::ScanKind const kind : kinds) {
            scan_both<Coupling, Shards>(ctx, kind, part, parallel_scan, single_scan);
            check_canonical_outputs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()});
        }
    }
}

// The count of a run of two grains and one element, so two shards of the
// scan each get the elements of one grain or more.
constexpr std::size_t kTwoGrains = 2 * ::fixy::concurrent::parallel_tree_grain + 1;

// THE INPUT OF A SCAN: a View of the run of out, shifted by one element,
// shares elements with out at a shift.  With no test of the overlap, shard 0
// reads in[16383], which is the element out[16384] that shard 1 writes.  So
// the scan ends the process before a shard starts.
void scan_shifted_input() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<float> run = ::foundation::core::mint_heap_array(tree::test_alloc(), kTwoGrains, 0.0f);
    run = ::foundation::core::with_mut(std::move(run), [&ctx](auto out) noexcept {
        View<float const> const whole{out.view()};
        View<float const> const in = whole.window(1, kTwoGrains - 1).expect("the input fits the run");
        auto [head, tail] = ::foundation::core::split_at(std::move(out), kTwoGrains - 1).expect("the cut fits the run");
        static_cast<void>(tail);
        ::fixy::concurrent::scan_sums_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 2>(
            ctx, tree::test_alloc(), ::foundation::core::ScanKind::Inclusive, in, std::move(head));
    });
}

// The control: the input can be the run of out, because each shard then reads
// only the elements that it writes.  The scan in place gives the bits of the
// scan of one thread into a run of its own.
[[nodiscard]] bool scan_in_place_matches() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<float> run = tree::draw_floats(kTwoGrains, 0x9A4A0008u);
    HeapArray<float> single = ::foundation::core::mint_heap_array(tree::test_alloc(), kTwoGrains, 0.0f);
    View<float const> const drawn{run.view()};
    single = ::foundation::core::with_mut(std::move(single), [drawn](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::scan_inclusive_sums(drawn, std::move(out)).is_ok());
    });
    run = ::foundation::core::with_mut(std::move(run), [&ctx](auto out) noexcept {
        View<float const> const in{out.view()};
        ::fixy::concurrent::scan_sums_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 2>(
            ctx, tree::test_alloc(), ::foundation::core::ScanKind::Inclusive, in, std::move(out));
    });
    return tree::same_runs(View<float const>{run.view()}, View<float const>{single.view()});
}

// The type route: an input that is a ViewMut of another frame proves by its
// brand that it shares no element with out (THE ALIAS PROOF of
// foundation/core/Frame.h), so the scan makes no test of the overlap.  The
// result has the bits of the scan of one thread.
[[nodiscard]] bool scan_branded_input_matches() {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    HeapArray<float> source = tree::draw_floats(kTwoGrains, 0x9A4A0009u);
    HeapArray<float> parallel = ::foundation::core::mint_heap_array(tree::test_alloc(), kTwoGrains, 0.0f);
    HeapArray<float> single = ::foundation::core::mint_heap_array(tree::test_alloc(), kTwoGrains, 0.0f);
    View<float const> const drawn{source.view()};
    single = ::foundation::core::with_mut(std::move(single), [drawn](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::scan_inclusive_sums(drawn, std::move(out)).is_ok());
    });
    source = ::foundation::core::with_mut(std::move(source), [&ctx, &parallel](auto input) noexcept {
        parallel = ::foundation::core::with_mut(std::move(parallel), [&ctx, &input](auto out) noexcept {
            static_assert(::foundation::core::DisjointInput<decltype(input), typename decltype(out)::brand_type>);
            ::fixy::concurrent::scan_sums_in_parallel<spawn::BodyCoupling::WaitOnEachOther, 2>(
                ctx, tree::test_alloc(), ::foundation::core::ScanKind::Inclusive, input, std::move(out));
        });
    });
    return tree::same_runs(View<float const>{parallel.view()}, View<float const>{single.view()});
}

void test_input_of_a_scan() {
    constexpr ::fixy::TextView kRefusal{"a parallel scan got an input that shares elements with its output at a shift"};
    ::fixy::check::expect_dies(kTest, scan_shifted_input, kRefusal);
    CRUCIBLE_TEST_ASSERT(kTest, scan_in_place_matches());
    CRUCIBLE_TEST_ASSERT(kTest, scan_branded_input_matches());
}

void test_nan_scans_are_canonical() {
    HeapArray<float> run = tree::draw_floats(tree::kLongest, 0x9A4A0006u);
    run = ::fixy::with_mut(std::move(run), [](auto elements) noexcept {
        ::crucible::test::put_specials(elements.view(), 0x9A4A0007u);
        *elements.get(0).expect("a test reads an element at or past the count of its View") =
            ::fixy::bit_cast<float>(0x7F800001u);
    });
    View<float const> const all{run.view()};
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 1>(all);
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 4>(all);
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 8>(all);
}

}  // namespace

int main() {
    test_input_of_a_scan();
    test_scans_on_each_thread_count();
    test_nan_scans_are_canonical();
    ::fixy::check::pass(kTest, "test_concurrent_parallel_tree_scan: all tests passed\n");
    return 0;
}
