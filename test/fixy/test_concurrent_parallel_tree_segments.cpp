// The parallel segmented sum of fixy/concurrent/ParallelTree.h, run for real
// on 1, 2, 4, 8 and 64 threads (parallel_tree_test.h).  Each sum must have the
// bits of sum_segments_by_tree of one thread.

#include <fixy/Core.h>
#include <fixy/concurrent/ParallelTree.h>
#include <foundation/core/Reduce.h>

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

// The parallel segmented sum gives the bits of sum_segments_by_tree, for
// segments of drawn lengths, some of them empty.
template <spawn::BodyCoupling Coupling, std::size_t Shards>
void check_segments(View<float const> all, ::crucible::test::PhiloxStream& stream) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    constexpr std::size_t kSegments = 300;
    std::size_t position = 0;
    HeapArray<std::size_t> const offsets = ::foundation::core::mint_heap_array_with<std::size_t>(
        tree::test_alloc(), kSegments + 1, [&stream, &position](std::size_t) noexcept {
            std::size_t const offset = position;
            position += stream.below(4) == 0 ? 0 : stream.below(9000);
            return offset;
        });
    View<std::size_t const> const offset_view = offsets.view();
    View<float const> const part =
        all.window(0, *offset_view.get(kSegments).expect("a test reads an element at or past the count of its View"))
            .expect("fits");
    HeapArray<float> parallel = ::foundation::core::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    HeapArray<float> single = ::foundation::core::mint_heap_array(tree::test_alloc(), kSegments, 0.0f);
    View<std::size_t const> const checked{offset_view};
    parallel = ::foundation::core::with_mut(std::move(parallel), [&](auto out) noexcept {
        ::fixy::concurrent::sum_segments_by_tree_in_parallel<Coupling, Shards>(ctx, part, checked, std::move(out));
    });
    single = ::foundation::core::with_mut(std::move(single), [&](auto out) noexcept {
        CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::sum_segments_by_tree(part, checked, std::move(out)).is_ok());
    });
    CRUCIBLE_TEST_ASSERT(kTest, tree::same_runs(View<float const>{parallel.view()}, View<float const>{single.view()}));
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
    test_segments_on_each_thread_count();
    ::fixy::check::pass(kTest, "test_concurrent_parallel_tree_segments: all tests passed\n");
    return 0;
}
