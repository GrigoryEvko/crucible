// The parallel sum scans of fixy/concurrent/ParallelTree.h, run for real on
// 1, 2, 4, 8 and 64 threads (parallel_tree_test.h).  Each output must have
// the bits of inclusive_sum_scan and exclusive_sum_scan of one thread.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Scan.h>

#include "../foundation/nan_outputs.h"
#include "../test_assert.h"
#include "parallel_tree_test.h"

#include <cstddef>

namespace {

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

// The parallel sum scans give the bits of the scans of one thread for each
// count.
template <spawn::BodyCoupling Coupling, std::size_t Shards, std::size_t Counts>
void check_scans(View<float const> all, ::fixy::FixedArray<std::size_t, Counts> const& counts) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        HeapArray<float> parallel_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        HeapArray<float> single_scan = ::foundation::core::mint_heap_array(tree::test_alloc(), count, 0.0f);
        ::fixy::concurrent::parallel_sum_scan<Coupling, Shards>(
            ctx, tree::test_alloc(), ::foundation::core::ScanKind::Inclusive, part, parallel_scan.view());
        assert(::foundation::core::inclusive_sum_scan(part, single_scan.view()).is_ok());
        assert(tree::same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
        ::fixy::concurrent::parallel_sum_scan<Coupling, Shards>(
            ctx, tree::test_alloc(), ::foundation::core::ScanKind::Exclusive, part, parallel_scan.view());
        assert(::foundation::core::exclusive_sum_scan(part, single_scan.view()).is_ok());
        assert(tree::same_runs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()}));
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
    for (auto const [left, right] : ::fixy::zip(parallel, single).expect("the two runs have one count")) {
        assert(::crucible::test::float_bits(*left) == ::crucible::test::output_bits(*right));
        assert(::crucible::test::float_bits(*right) == ::crucible::test::output_bits(*right));
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
            ::fixy::concurrent::parallel_sum_scan<Coupling, Shards>(ctx, tree::test_alloc(), kind, part,
                                                                    parallel_scan.view());
            bool const is_scanned = kind == ::foundation::core::ScanKind::Inclusive
                                      ? ::foundation::core::inclusive_sum_scan(part, single_scan.view()).is_ok()
                                      : ::foundation::core::exclusive_sum_scan(part, single_scan.view()).is_ok();
            assert(is_scanned);
            check_canonical_outputs(View<float const>{parallel_scan.view()}, View<float const>{single_scan.view()});
        }
    }
}

void test_nan_scans_are_canonical() {
    HeapArray<float> run = tree::draw_floats(tree::kLongest, 0x9A4A0006u);
    ::crucible::test::put_specials(run.view(), 0x9A4A0007u);
    ::crucible::test::element_at(run.view(), 0) = ::fixy::bit_cast<float>(0x7F800001u);
    View<float const> const all{run.view()};
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 1>(all);
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 4>(all);
    check_nan_scans<spawn::BodyCoupling::WaitOnEachOther, 8>(all);
}

}  // namespace

int main() {
    test_scans_on_each_thread_count();
    test_nan_scans_are_canonical();
    crucible::test::pass("test_concurrent_parallel_tree_scan: all tests passed\n");
    return 0;
}
