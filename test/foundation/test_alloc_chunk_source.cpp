// The chunk source of a window (the chunk source of foundation/alloc/Window.h),
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
//   - A window that moved has the empty record under its old name.  A second
//     source that takes the old name claims no chunk, and a set_owner of the
//     old name ends the process, while the window lives and after its end.
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
#include <utility>

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

// The chunk source of the eight threads and the counts of the claims.  The
// root of the allocator holds the source of the host window the same way: in
// an object of the process, which each thread reaches with no capture.  The
// window stays mapped to the end of the process.
constinit core::Immortal<alloc::ChunkSource> shared_source{};
constinit core::Atomic<std::uint32_t> shared_claims{0};
constinit core::Atomic<std::uint32_t> shared_refusals{0};

// The work of one thread: claim chunks until the window has none, and give
// each one the owner of the thread.  The count of the claims goes into the
// shared count, and a refused commit into the count of refusals.
struct Claimer {
    alloc::HeapId heap;

    void operator()() const noexcept {
        alloc::ChunkSource& source = *shared_source.get();
        alloc::UnitIndex const first_unit{core::StaticIndex<0>{}};
        std::uint32_t taken = 0;
        for (;;) {
            core::Option<alloc::ChunkIndex> const claimed = source.claim_chunk();
            if (claimed.is_none()) break;
            alloc::ChunkIndex const chunk = core::Option<alloc::ChunkIndex>{claimed}.value_or(alloc::ChunkIndex{0});
            bool const is_committed = source.commit_extent_of(chunk).is_ok();
            auto meta = source.start_chunk_meta(chunk);
            bool const is_slab_started = source.start_slab_meta(chunk, first_unit).is_ok();
            if (!is_committed || meta.is_err() || !is_slab_started) {
                static_cast<void>(shared_refusals.fetch_add_acq_rel(1));
                continue;
            }
            std::move(meta).expect("the test above found the record")->set_owner(alloc::owner_entry_of(heap));
            source.set_owner(chunk, alloc::owner_entry_of(heap));
            ++taken;
        }
        static_cast<void>(shared_claims.fetch_add_acq_rel(taken));
    }
};

void eight_threads_claim_each_chunk_once(TestRunnerCtx const& test) {
    shared_source.fill(reserve_test_window());
    Claimer one{alloc::HeapId{1}};
    Claimer two{alloc::HeapId{2}};
    Claimer three{alloc::HeapId{3}};
    Claimer four{alloc::HeapId{4}};
    Claimer five{alloc::HeapId{5}};
    Claimer six{alloc::HeapId{6}};
    Claimer seven{alloc::HeapId{7}};
    Claimer eight{alloc::HeapId{8}};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(one), std::move(two), std::move(three),
                               std::move(four), std::move(five), std::move(six), std::move(seven), std::move(eight))
        .expect("the host starts the eight threads of the test");
    CRUCIBLE_TEST_ASSERT(test, shared_refusals.load_acquire() == 0);
    CRUCIBLE_TEST_ASSERT(test, shared_claims.load_acquire() == kOwnableChunks);
    alloc::ChunkSource& source = *shared_source.get();
    alloc::WindowRecord const record = source.record();
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
        CRUCIBLE_TEST_ASSERT(test, source.is_extent_committed(chunk));
    }
    CRUCIBLE_TEST_ASSERT(test, owned == kOwnableChunks);
    CRUCIBLE_TEST_ASSERT(test, is_each_guard_free);
    CRUCIBLE_TEST_ASSERT(test, source.claim_chunk().is_none());
    pass(test, "  eight_threads_claim_each_chunk_once: PASSED\n");
}

void one_thread_claims_in_order(TestRunnerCtx const& test) {
    alloc::ChunkSource source{reserve_test_window()};
    std::uint32_t expected = 0;
    std::uint32_t claims = 0;
    bool is_in_order = true;
    for (;;) {
        core::Option<alloc::ChunkIndex> const claimed = source.claim_chunk();
        if (claimed.is_none()) break;
        if (alloc::is_guard_chunk(alloc::ChunkIndex{expected})) ++expected;
        alloc::ChunkIndex const chunk = core::Option<alloc::ChunkIndex>{claimed}.value_or(alloc::ChunkIndex{0});
        is_in_order = is_in_order && chunk == alloc::ChunkIndex{expected};
        ++expected;
        ++claims;
    }
    CRUCIBLE_TEST_ASSERT(test, is_in_order);
    CRUCIBLE_TEST_ASSERT(test, claims == kOwnableChunks);
    CRUCIBLE_TEST_ASSERT(test, source.claim_chunk().is_none());
    pass(test, "  one_thread_claims_in_order: PASSED\n");
}

// Each function below takes one window through two parameters, so the rule
// use-after-move does not see that the old name moved.

void claim_under_two_names(TestRunnerCtx const& test, alloc::Window& window, alloc::Window& old_name) {
    alloc::ChunkSource first{std::move(window)};
    alloc::ChunkSource second{std::move(old_name)};
    CRUCIBLE_TEST_ASSERT(test, first.claim_chunk().is_some());
    CRUCIBLE_TEST_ASSERT(test, second.claim_chunk().is_none());
    CRUCIBLE_TEST_ASSERT(test, second.record().is_empty());
}

void write_the_map_of_a_live_window(alloc::Window& window, alloc::Window& old_name) noexcept {
    alloc::ChunkSource source{std::move(window)};
    old_name.set_owner(alloc::ChunkIndex{0}, alloc::owner_entry_of(alloc::HeapId{7}));
}

void write_the_map_of_an_ended_window(alloc::Window& window, alloc::Window& old_name) noexcept {
    { alloc::ChunkSource source{std::move(window)}; }
    old_name.set_owner(alloc::ChunkIndex{0}, alloc::owner_entry_of(alloc::HeapId{7}));
}

// The text of the fatal invariant that the empty record of an old name breaks.
constexpr ::fixy::TextView kNoSuchChunk{"fatal invariant failed: chunk.raw() < record_.chunk_count()"};

void old_name_of_a_window_names_no_chunk(TestRunnerCtx const& test) {
    alloc::Window window = reserve_test_window();
    claim_under_two_names(test, window, window);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            alloc::Window live = reserve_test_window();
            write_the_map_of_a_live_window(live, live);
        },
        kNoSuchChunk);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            alloc::Window ended = reserve_test_window();
            write_the_map_of_an_ended_window(ended, ended);
        },
        kNoSuchChunk);
    pass(test, "  old_name_of_a_window_names_no_chunk: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    eight_threads_claim_each_chunk_once(test);
    one_thread_claims_in_order(test);
    old_name_of_a_window_names_no_chunk(test);
    pass(test, "test_alloc_chunk_source: PASSED\n");
    return 0;
}
