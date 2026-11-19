// The parallel tree reduce of fixy/concurrent/ParallelTree.h, run for real on
// 1, 2, 4, 8 and 64 threads (parallel_tree_test.h).  A combine that records
// the id of each thread that calls it proves the count of the threads, and
// the value must be the value of reduce_by_tree of one thread.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelTree.h>
#include <fixy/os/Process.h>
#include <foundation/core/Reduce.h>

#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "parallel_tree_test.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace spawn = ::fixy::spawn;
namespace tree = ::crucible::test::parallel_tree;

using ::foundation::core::HeapArray;
using ::foundation::core::View;

// The run of the recording combine that the calling thread last recorded.
// A thread records its id on its first call in each run, so a run makes one
// system call for each thread and not one for each combine.
constinit thread_local std::uint64_t recorded_generation = 0;

// The number of the next run of the recording combine.  Only the thread of
// main writes it.
constinit std::uint64_t next_generation = 0;

// A combine that is neither associative nor commutative, and that records
// the id of each thread that calls it in a table of slots.  A slot holds 0 or
// one id, and each id takes one slot.
struct RecordingMix {
    View<::fixy::Atomic<std::uint32_t>> slots;
    std::uint64_t generation = 0;

    [[nodiscard]] std::uint64_t operator()(std::uint64_t const& left, std::uint64_t const& right) const noexcept {
        record_thread();
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }

    void record_thread() const noexcept {
        if (recorded_generation == generation) return;
        recorded_generation = generation;
        ::fixy::TestRunnerCtx const runner{tree::eff::testing::test()};
        auto const id = static_cast<std::uint32_t>(::fixy::process::current_thread(runner));
        for (::fixy::Atomic<std::uint32_t>& slot : slots) {
            std::uint32_t const held = slot.load_acquire();
            if (held == id) return;
            if (held != 0) continue;
            if (slot.cas_acq_rel(0, id).is_ok()) return;
        }
        ::fixy::fatal("the table of the threads of a combine is full");
    }
};

// The count of the slots that hold an id.
[[nodiscard]] std::size_t threads_in(View<::fixy::Atomic<std::uint32_t>> slots) noexcept {
    std::size_t count = 0;
    for (::fixy::Atomic<std::uint32_t>& slot : slots) {
        if (slot.load_acquire() != 0) ++count;
    }
    return count;
}

// The parallel tree reduce with the recording combine gives the value of
// reduce_by_tree, and the combine ran on Shards threads and on the calling
// thread.  One shard runs on the calling thread alone.
template <std::size_t Shards>
void check_reduce_on_stated_threads(View<std::uint64_t const> all) noexcept {
    tree::BgCtx const ctx{tree::eff::testing::bg()};
    // One grain for each shard and a short last grain, so each shard holds at
    // least one grain of combines.
    View<std::uint64_t const> const run =
        all.window(0, Shards * ::fixy::concurrent::parallel_tree_grain + 7).expect("the run fits");
    HeapArray<::fixy::Atomic<std::uint32_t>> table =
        ::foundation::core::mint_heap_array_with<::fixy::Atomic<std::uint32_t>>(
            tree::test_alloc(), 256, [](std::size_t) noexcept { return ::fixy::Atomic<std::uint32_t>{0}; });
    RecordingMix const mix{table.view(), ++next_generation};
    auto const parallel = ::fixy::concurrent::reduce_by_tree_in_parallel<spawn::BodyCoupling::WaitOnEachOther, Shards>(
        ctx, tree::test_alloc(), run, mix);
    std::size_t const threads = threads_in(table.view());
    std::uint64_t const single = ::foundation::core::reduce_by_tree(run, mix).expect("the run has elements");
    CRUCIBLE_TEST_ASSERT(kTest, parallel.is_some());
    for (std::uint64_t const value : parallel) {
        CRUCIBLE_TEST_ASSERT(kTest, value == single);
    }
    CRUCIBLE_TEST_ASSERT(kTest, threads == (Shards == 1 ? 1 : Shards + 1));
}

// Each count of threads gives the value of the reduce of one thread, and
// two runs of 64 threads give two schedules of the operating system.
void test_reduce_on_stated_threads() {
    ::crucible::test::PhiloxStream stream{0x9A4A0004u};
    HeapArray<std::uint64_t> run =
        ::foundation::core::mint_heap_array(tree::test_alloc(), tree::kLongest, std::uint64_t{0});
    for (std::uint64_t& value : run) {
        value = stream.next_wide();
    }
    View<std::uint64_t const> const all{run.view()};
    check_reduce_on_stated_threads<1>(all);
    check_reduce_on_stated_threads<2>(all);
    check_reduce_on_stated_threads<4>(all);
    check_reduce_on_stated_threads<8>(all);
    for (int repeat = 0; repeat < 2; ++repeat) {
        check_reduce_on_stated_threads<64>(all);
    }
}

}  // namespace

int main() {
    test_reduce_on_stated_threads();
    ::fixy::check::pass(kTest, "test_concurrent_reduce_by_tree_in_parallel: all tests passed\n");
    return 0;
}
