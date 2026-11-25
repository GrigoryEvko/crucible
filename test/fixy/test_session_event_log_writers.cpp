// The one writer of a session event log (fixy/session/EventLog.h).
//
// A log has one writer.  A second writer for one log ends the process with
// Event_Log_Writer_Twice, so no two recorders append to one log from two
// threads.  Each endpoint records into a log of its own, on its own
// thread, and merge_session_logs reads the logs as one sequence: by step,
// and for one step in the order of the arguments.  The test runs two
// recorded sessions on two threads many times, and each merge gives the
// same sequence.  The tsan preset runs the same test, so the shape with
// one log for each thread is the shape that ThreadSanitizer checks.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace test_session_event_log_writers {

namespace s = ::fixy::session;

// The count of the sends of each recorded session.
constexpr int kSends = 64;

// The count of the runs of the two threads.
constexpr int kRuns = 32;

constexpr s::RoleTagId kLeft{1};
constexpr s::RoleTagId kRight{2};
constexpr s::RoleTagId kSink{3};

// The allocation capability of the test, which the writer of each log takes.
[[nodiscard]] constexpr ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The Resource of a session that sends ints into a sum.
struct Tally {
    std::int64_t sum = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

using Counting = s::Loop<s::Send<int, s::Continue>>;

namespace {

// Records kSends sends of one session into the log of `writer`, then
// detaches the session.  The session and its log belong to the calling
// thread.
void record_one_session(s::SessionEventWriter writer, s::RoleTagId self) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_recorded_session(s::mint_session<Counting>(ctx, Tally{}), std::move(writer), self, kSink);
    for (int value = 0; value < kSends; ++value) {
        handle = std::move(handle).send(value, [](Tally& tally, int& sent) noexcept {
            tally.sum += sent;
            return true;
        });
    }
    std::move(handle).detach(s::detach_reason::InfiniteLoopProtocol{});
}

// A log has one writer.  The second mint for it ends the process.  The mint
// runs in a child through expect_dies of the Check family.
void a_second_writer_of_one_log_ends_the_process() {
    s::SessionEventLog log;
    s::SessionEventWriter first = s::mint_session_event_writer(test_alloc(), log);
    ::fixy::check::expect_dies(
        ::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
        [&log] noexcept { static_cast<void>(s::mint_session_event_writer(test_alloc(), log)); },
        ::fixy::TextView{"[Event_Log_Writer_Twice]: a second writer was minted for a session event log that "
                         "already has one"});
    first.record_now(s::SessionEvent::close(kLeft, kSink));
    CRUCIBLE_TEST_ASSERT(kTest, log.size() == 1);
}

// Two recorded sessions on two threads, each with a log of its own.  After
// the join, the merge reads the two logs by step, and for one step the left
// log first.  So each run gives the same sequence.
void two_threads_record_into_their_own_logs() {
    for (int run = 0; run < kRuns; ++run) {
        s::SessionEventLog left_log{s::SessionTagId{1}};
        s::SessionEventLog right_log{s::SessionTagId{2}};
        {
            std::jthread left{record_one_session, s::mint_session_event_writer(test_alloc(), left_log), kLeft};
            std::jthread right{record_one_session, s::mint_session_event_writer(test_alloc(), right_log), kRight};
        }
        CRUCIBLE_TEST_ASSERT(kTest, left_log.size() == kSends + 1);
        CRUCIBLE_TEST_ASSERT(kTest, right_log.size() == kSends + 1);
        const auto merged = s::merge_session_logs(test_alloc(), left_log, right_log);
        CRUCIBLE_TEST_ASSERT(kTest, merged.size() == 2 * (kSends + 1));
        for (std::size_t place = 0; place < merged.size(); ++place) {
            const s::SessionEvent& event = *merged.get(place).expect("the merge holds each event of the two logs");
            const bool is_left = place % 2 == 0;
            const bool is_last_step = place / 2 == kSends;
            CRUCIBLE_TEST_ASSERT(kTest, event.step_id().raw() == place / 2 + 1);
            CRUCIBLE_TEST_ASSERT(kTest, event.session().value == (is_left ? 1u : 2u));
            CRUCIBLE_TEST_ASSERT(kTest, event.from_role() == (is_left ? kLeft : kRight));
            CRUCIBLE_TEST_ASSERT(kTest, event.op() == (is_last_step ? s::SessionOp::Detach : s::SessionOp::Send));
        }
    }
}

// An empty log adds nothing, and a tie of steps keeps the order of the
// arguments.
void a_merge_keeps_the_order_of_the_arguments_for_one_step() {
    s::SessionEventLog empty_log;
    s::SessionEventLog first_log{s::SessionTagId{7}};
    s::SessionEventLog second_log{s::SessionTagId{8}};
    s::SessionEventWriter first = s::mint_session_event_writer(test_alloc(), first_log);
    s::SessionEventWriter second = s::mint_session_event_writer(test_alloc(), second_log);
    second.record_now(s::SessionEvent::close(kRight, kSink));
    first.record_now(s::SessionEvent::close(kLeft, kSink));
    first.record_now(s::SessionEvent::close(kLeft, kSink));
    const auto merged = s::merge_session_logs(test_alloc(), empty_log, second_log, first_log);
    CRUCIBLE_TEST_ASSERT(kTest, merged.size() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, merged.get(0).expect("three events")->session().value == 8);
    CRUCIBLE_TEST_ASSERT(kTest, merged.get(1).expect("three events")->session().value == 7);
    CRUCIBLE_TEST_ASSERT(kTest, merged.get(2).expect("three events")->step_id().raw() == 2);
}

}  // namespace

}  // namespace test_session_event_log_writers

int main() {
    using namespace test_session_event_log_writers;
    a_second_writer_of_one_log_ends_the_process();
    two_threads_record_into_their_own_logs();
    a_merge_keeps_the_order_of_the_arguments_for_one_step();
    ::fixy::check::pass(kTest, "test_session_event_log_writers: OK\n");
    return 0;
}
