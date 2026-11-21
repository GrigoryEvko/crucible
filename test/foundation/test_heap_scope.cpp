// The typed testing door of foundation/heap/HeapScope.h.
//
//   - A scope of a new thread claims the heap of the thread, and its blocks
//     of the classes 1, 8, 32, 33 and 52 lie in chunks of that heap.
//   - The census of the scope counts each block that the body holds, and
//     after the release of each block and a flush of the bins, no slot of the
//     heap is taken.
//   - Two scopes of the thread, one after the other, name the same heap.

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/heap/HeapScope.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

namespace alloc = ::foundation::alloc;
namespace heap = ::foundation::heap;

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body)
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

// True when the heap of the calling thread owns the chunk of the block.
[[nodiscard]] bool is_own_block(alloc::AddressWord block) noexcept {
    alloc::HeapState const& current = alloc::current_heap();
    return current.chunk_map().owner_of(block) == current.self();
}

void scope_gives_and_takes_blocks(TestRunnerCtx const& test) {
    bool const is_sound = on_other_thread([] noexcept {
        bool const was_sentinel = alloc::is_sentinel(alloc::current_heap());
        return heap::mint_heap_scope(
            ::foundation::effects::testing::test().alloc, [was_sentinel](auto& scope) noexcept {
                auto first = scope.template allocate<alloc::SizeClass{1}>();
                auto eighth = scope.template allocate<alloc::SizeClass{8}>();
                auto small_last = scope.template allocate<alloc::SizeClass{32}>();
                auto medium_first = scope.template allocate<alloc::SizeClass{33}>();
                auto medium_last = scope.template allocate<alloc::SizeClass{52}>();
                bool const are_own = is_own_block(first.address()) && is_own_block(eighth.address())
                                  && is_own_block(small_last.address()) && is_own_block(medium_first.address())
                                  && is_own_block(medium_last.address());
                alloc::HeapCensus const held = scope.census();
                bool const counts_held = held.taken_slots().raw() == 5 + held.binned_blocks().raw();
                scope.release(std::move(first));
                scope.release(std::move(eighth));
                scope.release(std::move(small_last));
                scope.release(std::move(medium_first));
                scope.release(std::move(medium_last));
                scope.flush_bins();
                alloc::HeapCensus const after = scope.census();
                bool const is_empty = after.taken_slots().raw() == 0 && after.chunks_with_taken_slots().raw() == 0
                                   && after.empty_slabs().raw() == after.slabs().raw();
                return was_sentinel && !alloc::is_sentinel(alloc::current_heap()) && are_own && counts_held && is_empty;
            });
    });
    CRUCIBLE_TEST_ASSERT(test, is_sound);
    pass(test, "  scope_gives_and_takes_blocks: PASSED\n");
}

void two_scopes_name_one_heap(TestRunnerCtx const& test) {
    std::uint16_t const first_entry =
        heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
            auto block = scope.template allocate<alloc::SizeClass{4}>();
            auto const entry = static_cast<std::uint16_t>(alloc::current_heap().chunk_map().owner_of(block.address()));
            scope.release(std::move(block));
            return entry;
        });
    std::uint16_t const second_entry =
        heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
            auto block = scope.template allocate<alloc::SizeClass{4}>();
            auto const entry = static_cast<std::uint16_t>(alloc::current_heap().chunk_map().owner_of(block.address()));
            scope.release(std::move(block));
            return entry;
        });
    CRUCIBLE_TEST_ASSERT(test, first_entry != 0 && first_entry == second_entry);
    CRUCIBLE_TEST_ASSERT(test, first_entry == static_cast<std::uint16_t>(alloc::current_heap().self()));
    pass(test, "  two_scopes_name_one_heap: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    scope_gives_and_takes_blocks(test);
    two_scopes_name_one_heap(test);
    pass(test, "test_heap_scope: PASSED\n");
    return 0;
}
