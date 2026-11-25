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
#include <foundation/Quarantine.h>
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
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body))
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

// The text of the fatal invariant that an emptied block breaks at the release.
constexpr ::fixy::TextView kEmptiedBlock{"fatal invariant failed: held.address_ != ::foundation::alloc::AddressWord{}"};

// Two parameters name one block, so the rule use-after-move does not see that
// the old name moved.  Each function below ends the process: at the release of
// the old name, and else at its own end.  The kind two-names of the rule
// view-escape refuses each call that gives one block to two parameters, so
// each such call of a test stands in a PROBE region.
//
// The block is released under the first name, and then under the old name.
// With a block that a release leaves full, the bin gives one block to two
// owners.
template <class Scope, class Block>
[[noreturn]] void release_twice(Scope& scope, Block& block, Block& old_name) noexcept {
    scope.release(std::move(block));
    scope.release(std::move(old_name));
    ::fixy::fatal("the block was released two times");
}

// The block moves out under the first name, and the release of the old name
// follows.
template <class Scope, class Block>
[[noreturn]] void release_the_old_name(Scope& scope, Block& block, Block& old_name) noexcept {
    Block moved{std::move(block)};
    scope.release(std::move(old_name));
    scope.release(std::move(moved));
    ::fixy::fatal("the old name of a moved block was released");
}

// The block moves out under the first name by an assignment over an empty name,
// and the release of the old name follows.
template <class Scope, class Block>
[[noreturn]] void release_the_old_name_after_an_assignment(Scope& scope, Block& block, Block& old_name,
                                                           Block& target) noexcept {
    scope.release(std::move(target));
    target = std::move(block);
    scope.release(std::move(old_name));
    scope.release(std::move(target));
    ::fixy::fatal("the old name of an assigned block was released");
}

// A move and a release empty a block, so the release of the old name ends the
// process.  Each violation runs in a child process, which ends by the abort of
// the fatal invariant.
void old_name_of_a_block_ends_the_process(TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            static_cast<void>(
                heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
                    auto block = scope.template allocate<alloc::SizeClass{4}>();
                    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one block, so a release ends the process")
                    release_twice(scope, block, block);
                    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
                    return 0;
                }));
        },
        kEmptiedBlock);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            static_cast<void>(
                heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
                    auto block = scope.template allocate<alloc::SizeClass{4}>();
                    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one block, so a release ends the process")
                    release_the_old_name(scope, block, block);
                    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
                    return 0;
                }));
        },
        kEmptiedBlock);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            static_cast<void>(
                heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
                    auto block = scope.template allocate<alloc::SizeClass{4}>();
                    auto target = scope.template allocate<alloc::SizeClass{4}>();
                    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: two parameters get one block, so a release ends the process")
                    release_the_old_name_after_an_assignment(scope, block, block, target);
                    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
                    return 0;
                }));
        },
        kEmptiedBlock);
    pass(test, "  old_name_of_a_block_ends_the_process: PASSED\n");
}

// The release of a HeapState takes each raw word, also the address 0, and only
// a flush checks it.  So a scope can give a block at the address 0.  The
// optimizer has no fact about a word of a bin, so the release of that block
// ends the process in each build.
void block_at_address_zero_ends_the_process(TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            static_cast<void>(
                heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
                    alloc::current_heap().release(alloc::SizeClass{4}, alloc::AddressWord{});
                    auto block = scope.template allocate<alloc::SizeClass{4}>();
                    scope.release(std::move(block));
                    return 0;
                }));
        },
        kEmptiedBlock);
    pass(test, "  block_at_address_zero_ends_the_process: PASSED\n");
}

// A block that moved releases as the block that it came from, and two blocks
// that a scope holds at one time have two addresses.
void moved_block_releases(TestRunnerCtx const& test) {
    bool const is_sound = heap::mint_heap_scope(::foundation::effects::testing::test().alloc, [](auto& scope) noexcept {
        auto first = scope.template allocate<alloc::SizeClass{4}>();
        auto second = scope.template allocate<alloc::SizeClass{4}>();
        bool const are_apart = first.address() != second.address();
        alloc::AddressWord const address = first.address();
        auto moved = std::move(first);
        bool const is_kept = moved.address() == address;
        scope.release(std::move(moved));
        auto again = scope.template allocate<alloc::SizeClass{4}>();
        bool const is_reused = again.address() == address;
        scope.release(std::move(again));
        scope.release(std::move(second));
        return are_apart && is_kept && is_reused;
    });
    CRUCIBLE_TEST_ASSERT(test, is_sound);
    pass(test, "  moved_block_releases: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    scope_gives_and_takes_blocks(test);
    two_scopes_name_one_heap(test);
    moved_block_releases(test);
    old_name_of_a_block_ends_the_process(test);
    block_at_address_zero_ends_the_process(test);
    pass(test, "test_heap_scope: PASSED\n");
    return 0;
}
