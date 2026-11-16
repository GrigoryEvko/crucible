// The parallel tree sum of fixy/concurrent/ParallelTree.h, run for real on 1,
// 2, 4, 8 and 64 threads (parallel_tree_test.h).  Each result must have the
// bits of tree_sum of one thread.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Reduce.h>

#include "../test_assert.h"
#include "parallel_tree_test.h"

#include <cstddef>

namespace {

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

// The parallel tree sum gives the bits of tree_sum for each count.
template <spawn::BodyCoupling Coupling, std::size_t Shards, std::size_t Counts>
void check_sums(View<float const> all, ::fixy::FixedArray<std::size_t, Counts> const& counts) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    for (std::size_t const count : counts) {
        View<float const> const part = all.window(0, count).expect("the part fits the run");
        float const parallel = ::fixy::concurrent::parallel_tree_sum<Coupling, Shards>(ctx, tree::test_alloc(), part);
        assert(tree::same_bits(parallel, ::foundation::core::tree_sum(part)));
    }
}

void test_sums_on_each_thread_count() {
    HeapArray<float> const run = tree::draw_floats(tree::kLongest, 0x9A4A0001u);
    View<float const> const all{run.view()};
    check_sums<spawn::BodyCoupling::WaitOnEachOther, 1>(all, tree::kCounts);
    check_sums<spawn::BodyCoupling::WaitOnEachOther, 2>(all, tree::kCounts);
    check_sums<spawn::BodyCoupling::WaitOnEachOther, 4>(all, tree::kCounts);
    check_sums<spawn::BodyCoupling::WaitOnEachOther, 8>(all, tree::kCounts);
    check_sums<spawn::BodyCoupling::WaitOnEachOther, 64>(all, tree::kWideCounts);
    check_sums<spawn::BodyCoupling::Independent, 8>(all, tree::kCounts);
}

}  // namespace

int main() {
    test_sums_on_each_thread_count();
    crucible::test::pass("test_concurrent_parallel_tree: all tests passed\n");
    return 0;
}
