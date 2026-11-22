// The root of foundation/alloc/Root.h, the sentinel heap and the cell of a
// thread of foundation/alloc/HeapState.h, in one process.
//
//   - Before the init, the cell of each thread names the sentinel, the
//     sentinel holds no map and the entry none, and the root is not ready.
//   - After ensure_root, the root is ready, the host slot holds the host
//     window, and the sentinel holds the chunk map of that window.  Its entry
//     stays none, and the cell of the thread still names the sentinel.
//   - A new thread reads the sentinel through its cell before its first use.
//   - Eight threads, one after the other, call ensure_root and see the one
//     host window of the first init.
//   - The claims of two threads take the heaps of the ids 1 and 2 of the heap
//     table, and a second claim of a thread gives its heap back.
//
// Each case after the first needs the init, and the init holds the window to
// the end of the process, so the first case runs first.

#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Root.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body))
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

void sentinel_before_the_init(TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, !alloc::alloc_root_.is_ready());
    CRUCIBLE_TEST_ASSERT(test, alloc::is_sentinel(alloc::current_heap()));
    CRUCIBLE_TEST_ASSERT(test, alloc::sentinel_heap_.chunk_map().is_empty());
    CRUCIBLE_TEST_ASSERT(test, alloc::sentinel_heap_.self() == alloc::ChunkOwner::none);
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.window(alloc::kHostWindow).is_empty());
    pass(test, "  sentinel_before_the_init: PASSED\n");
}

void init_writes_the_sentinel(TestRunnerCtx const& test) {
    alloc::ensure_root();
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.is_ready());
    alloc::WindowRecord const host = alloc::alloc_root_.window(alloc::kHostWindow);
    CRUCIBLE_TEST_ASSERT(test, !host.is_empty());
    CRUCIBLE_TEST_ASSERT(test, alloc::sentinel_heap_.chunk_map() == host.chunk_map());
    CRUCIBLE_TEST_ASSERT(test, alloc::sentinel_heap_.self() == alloc::ChunkOwner::none);
    CRUCIBLE_TEST_ASSERT(test, alloc::is_sentinel(alloc::current_heap()));
    // The map of the sentinel reads the empty entry of each chunk of the new
    // window, the first and the last.
    alloc::ChunkIndex const last{static_cast<std::uint32_t>(host.chunk_count() - 1)};
    CRUCIBLE_TEST_ASSERT(test, alloc::current_heap().chunk_map().owner_of(host.address_of(alloc::ChunkIndex{0}))
                                   == alloc::ChunkOwner::none);
    CRUCIBLE_TEST_ASSERT(test,
                         alloc::current_heap().chunk_map().owner_of(host.address_of(last)) == alloc::ChunkOwner::none);
    pass(test, "  init_writes_the_sentinel: PASSED\n");
}

void new_thread_reads_the_sentinel(TestRunnerCtx const& test) {
    bool const is_sentinel = on_other_thread([] noexcept { return alloc::is_sentinel(alloc::current_heap()); });
    CRUCIBLE_TEST_ASSERT(test, is_sentinel);
    pass(test, "  new_thread_reads_the_sentinel: PASSED\n");
}

void threads_see_one_window(TestRunnerCtx const& test) {
    alloc::AddressWord const base = alloc::alloc_root_.window(alloc::kHostWindow).base();
    bool is_same = true;
    for (std::uint32_t round = 0; round < 8; ++round) {
        bool const is_round_same = on_other_thread([base] noexcept {
            alloc::ensure_root();
            return alloc::alloc_root_.window(alloc::kHostWindow).base() == base;
        });
        is_same = is_same && is_round_same;
    }
    CRUCIBLE_TEST_ASSERT(test, is_same);
    pass(test, "  threads_see_one_window: PASSED\n");
}

// THE HEAP TABLE: the first claim of the process takes slot 0, the heap of
// the id 1, with the chunk map of the host window and its own entry.  A
// second claim of the thread gives the same heap.  A claim on a new thread
// takes the id 2.  The slots commit one step of 2 MiB from the front.
void claims_take_the_ids_in_order(TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.claimed_heaps() == 0);
    alloc::HeapState& heap = alloc::claim_thread_heap();
    CRUCIBLE_TEST_ASSERT(test, !alloc::is_sentinel(heap) && &alloc::current_heap() == &heap);
    CRUCIBLE_TEST_ASSERT(test, heap.self() == alloc::owner_entry_of(alloc::HeapId{1}));
    CRUCIBLE_TEST_ASSERT(test, heap.chunk_map() == alloc::alloc_root_.window(alloc::kHostWindow).chunk_map());
    CRUCIBLE_TEST_ASSERT(test, &alloc::claim_thread_heap() == &heap);
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.claimed_heaps() == 1);
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.committed_heap_bytes().raw() == alloc::kHeapTableStepBytes);
    std::uint16_t const other = on_other_thread([] noexcept {
        bool const was_sentinel = alloc::is_sentinel(alloc::current_heap());
        std::uint16_t const entry = static_cast<std::uint16_t>(alloc::claim_thread_heap().self());
        return was_sentinel ? entry : std::uint16_t{0};
    });
    CRUCIBLE_TEST_ASSERT(test, other == 2);
    CRUCIBLE_TEST_ASSERT(test, alloc::alloc_root_.claimed_heaps() == 2);
    CRUCIBLE_TEST_ASSERT(test, &alloc::current_heap() == &heap);
    pass(test, "  claims_take_the_ids_in_order: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    sentinel_before_the_init(test);
    init_writes_the_sentinel(test);
    new_thread_reads_the_sentinel(test);
    threads_see_one_window(test);
    claims_take_the_ids_in_order(test);
    pass(test, "test_alloc_root: PASSED\n");
    return 0;
}
