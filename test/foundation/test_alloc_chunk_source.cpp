// The chunk source of a window (THE CHUNK SOURCE of foundation/alloc/Window.h),
// with eight threads that claim and commit chunks at the same time.
//
//   - Eight threads claim each chunk of a window of 4 GiB that is not a
//     guard, and each one commits the extent of its chunk, starts the record
//     of the chunk and of its first unit, and writes the entry of the chunk.
//     The claims sum to the count of the chunks that are not guards, so no
//     chunk has two claims and no chunk stays free.
//   - The entry of each chunk names one of the eight threads.
//   - The bit of each extent is set, so no commit of an extent lost its bit.
//   - A guard chunk has no owner, and a claim after the last chunk gives none.
//   - One thread claims the chunks of a second window in the order of their
//     index, with the guard of each extent skipped.
//
// The test runs in the default and the tsan presets, with no suppression.

#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/Metadata.h>
#include <foundation/alloc/Window.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Immortal.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/PageSource.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace alloc = ::foundation::alloc;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

constexpr std::size_t kWindowBytes = std::size_t{4} << 30;
// The chunks of a window of 4 GiB that are not guards: 4 extents of 511.
constexpr std::uint32_t kOwnableChunks = 4 * (alloc::kChunksPerExtent - 1);

[[nodiscard]] alloc::Window reserve_test_window() noexcept {
    alloc::WindowDiagnostic diagnostic{};
    return alloc::reserve_window(os::ReserveCap{kWindowBytes}, core::to(diagnostic))
        .expect("the ladder gives a window of 4 GiB under the cap");
}

// The window of the eight threads, its commits and the counts of the claims.
// The root of the allocator holds the host window and its commits the same
// way: in objects of the process, which each thread reaches with no capture.
// The window stays mapped to the end of the process.
constinit core::Immortal<alloc::Window> shared_window{};
constinit alloc::WindowCommits shared_commits{};
constinit core::Atomic<std::uint32_t> shared_claims{0};
constinit core::Atomic<std::uint32_t> shared_refusals{0};

// The work of one thread: claim chunks until the window has none, and give
// each one the owner of the thread.  The count of the claims goes into the
// shared count, and a refused commit into the count of refusals.
struct Claimer {
    alloc::HeapId heap;

    void operator()() const noexcept {
        alloc::Window& window = *shared_window.get();
        alloc::WindowCommits& commits = shared_commits;
        alloc::UnitIndex const first_unit{core::StaticIndex<0>{}};
        std::uint32_t taken = 0;
        for (;;) {
            core::Option<alloc::ChunkIndex> const claimed = window.claim_chunk(commits);
            if (claimed.is_none()) break;
            alloc::ChunkIndex const chunk = core::Option<alloc::ChunkIndex>{claimed}.value_or(alloc::ChunkIndex{0});
            bool const is_committed = window.commit_extent_of(commits, chunk).is_ok();
            auto meta = window.start_chunk_meta(commits, chunk);
            bool const is_slab_started = window.start_slab_meta(commits, chunk, first_unit).is_ok();
            if (!is_committed || meta.is_err() || !is_slab_started) {
                static_cast<void>(shared_refusals.fetch_add_acq_rel(1));
                continue;
            }
            std::move(meta).expect("the test above found the record")->set_owner(alloc::owner_entry_of(heap));
            window.set_owner(chunk, alloc::owner_entry_of(heap));
            ++taken;
        }
        static_cast<void>(shared_claims.fetch_add_acq_rel(taken));
    }
};

void eight_threads_claim_each_chunk_once(TestRunnerCtx const& test) {
    shared_window.fill(reserve_test_window());
    Claimer one{alloc::HeapId{1}};
    Claimer two{alloc::HeapId{2}};
    Claimer three{alloc::HeapId{3}};
    Claimer four{alloc::HeapId{4}};
    Claimer five{alloc::HeapId{5}};
    Claimer six{alloc::HeapId{6}};
    Claimer seven{alloc::HeapId{7}};
    Claimer eight{alloc::HeapId{8}};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, one, two, three, four, five, six, seven, eight)
        .expect("the host starts the eight threads of the test");
    CRUCIBLE_TEST_ASSERT(test, shared_refusals.load_acquire() == 0);
    CRUCIBLE_TEST_ASSERT(test, shared_claims.load_acquire() == kOwnableChunks);
    alloc::Window const& window = *shared_window.get();
    alloc::WindowCommits const& commits = shared_commits;
    alloc::WindowRecord const record = window.record();
    std::uint32_t owned = 0;
    bool is_each_guard_free = true;
    for (std::uint32_t index = 0; index < record.chunk_count(); ++index) {
        alloc::ChunkIndex const chunk{index};
        alloc::ChunkOwner const entry = record.chunk_map().owner_of(record.address_of(chunk));
        if (alloc::is_guard_chunk(chunk)) {
            is_each_guard_free = is_each_guard_free && entry == alloc::ChunkOwner::none;
            continue;
        }
        bool const is_thread_entry = entry != alloc::ChunkOwner::none && static_cast<std::uint16_t>(entry) <= 8;
        if (is_thread_entry) ++owned;
        CRUCIBLE_TEST_ASSERT(test, window.is_extent_committed(commits, chunk));
    }
    CRUCIBLE_TEST_ASSERT(test, owned == kOwnableChunks);
    CRUCIBLE_TEST_ASSERT(test, is_each_guard_free);
    CRUCIBLE_TEST_ASSERT(test, window.claim_chunk(shared_commits).is_none());
    pass(test, "  eight_threads_claim_each_chunk_once: PASSED\n");
}

void one_thread_claims_in_order(TestRunnerCtx const& test) {
    alloc::Window const window = reserve_test_window();
    alloc::WindowCommits commits{};
    std::uint32_t expected = 0;
    std::uint32_t claims = 0;
    bool is_in_order = true;
    for (;;) {
        core::Option<alloc::ChunkIndex> const claimed = window.claim_chunk(commits);
        if (claimed.is_none()) break;
        if (alloc::is_guard_chunk(alloc::ChunkIndex{expected})) ++expected;
        alloc::ChunkIndex const chunk = core::Option<alloc::ChunkIndex>{claimed}.value_or(alloc::ChunkIndex{0});
        is_in_order = is_in_order && chunk == alloc::ChunkIndex{expected};
        ++expected;
        ++claims;
    }
    CRUCIBLE_TEST_ASSERT(test, is_in_order);
    CRUCIBLE_TEST_ASSERT(test, claims == kOwnableChunks);
    CRUCIBLE_TEST_ASSERT(test, window.claim_chunk(commits).is_none());
    pass(test, "  one_thread_claims_in_order: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    eight_threads_claim_each_chunk_once(test);
    one_thread_claims_in_order(test);
    pass(test, "test_alloc_chunk_source: PASSED\n");
    return 0;
}
