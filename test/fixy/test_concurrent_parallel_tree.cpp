// The parallel tree sum and the parallel segmented sum of
// fixy/concurrent/ParallelTree.h, run for real on 1, 2, 4, 8 and 64 threads
// (parallel_tree_test.h).  Each result must have the bits of tree_sum and of
// segmented_tree_sum of one thread.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Reduce.h>

#include "../test_assert.h"
#include "parallel_tree_test.h"

#include <cstddef>
#include <cstdint>

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

// The parallel segmented sum gives the bits of segmented_tree_sum, for
// segments of drawn lengths, some of them empty.
template <spawn::BodyCoupling Coupling, std::size_t Shards>
void check_segments(View<float const> all, ::crucible::test::PhiloxStream& stream) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    constexpr std::size_t kSegments = 300;
    HeapArray<std::size_t> offsets =
        ::foundation::core::mint_heap_array(tree::test_alloc(), kSegments + 1, std::size_t{0});
    View<std::size_t> const offset_view = offsets.view();
    std::size_t position = 0;
    for (std::size_t segment = 0; segment <= kSegments; ++segment) {
        ::crucible::test::element_at(offset_view, segment) = position;
        position += stream.below(4) == 0 ? 0 : stream.below(9000);
    }
    View<float const> const part = all.window(0, ::crucible::test::element_at(offset_view, kSegments)).expect("fits");
    HeapArray<float> parallel = ::foundation::core::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    HeapArray<float> single = ::foundation::core::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    View<std::size_t const> const checked{offset_view};
    ::fixy::concurrent::parallel_segmented_tree_sum<Coupling, Shards>(ctx, part, checked, parallel.view());
    assert(::foundation::core::segmented_tree_sum(part, checked, single.view()).is_ok());
    assert(tree::same_runs(View<float const>{parallel.view()}, View<float const>{single.view()}));
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

void test_segments_on_each_thread_count() {
    HeapArray<float> const run = tree::draw_floats(300 * 9000, 0x9A4A0002u);
    View<float const> const all{run.view()};
    ::crucible::test::PhiloxStream stream{0x9A4A0003u};
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 1>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 2>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 4>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 8>(all, stream);
    check_segments<spawn::BodyCoupling::WaitOnEachOther, 64>(all, stream);
    check_segments<spawn::BodyCoupling::Independent, 8>(all, stream);
}

}  // namespace

int main() {
    test_sums_on_each_thread_count();
    test_segments_on_each_thread_count();
    crucible::test::pass("test_concurrent_parallel_tree: all tests passed\n");
    return 0;
}
