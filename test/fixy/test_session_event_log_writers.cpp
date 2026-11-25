// The one writer of a session event log (fixy/session/EventLog.h).
//
// A log has one writer.  A second writer for one log ends the process with
// Event_Log_Writer_Twice, so no two recorders append to one log from two
// threads.  Each endpoint records into a log of its own, on its own
// thread, and merge_session_logs reads the logs as one sequence: by step,
// and for one step in the order of the arguments.  The test runs two
// recorded sessions on two threads many times, and each merge gives the
// same sequence.  The tsan preset runs the same test, so the shape with
// one log for each thread is the shape that ThreadSanitizer checks.  Two
// threads then read one finished log at the same time, and one thread reads
// and merges a log while its writer appends.

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
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
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

// A writer appends while the same thread holds an event and a View of the
// events, and while a loop reads the log.  No append moves an event, so each
// borrow stays correct, and the loop sees the events of its start.
void an_append_keeps_each_event_at_its_address() {
    s::SessionEventLog log{s::SessionTagId{4}};
    s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
    writer.record_now(s::SessionEvent::close(kLeft, kSink));
    writer.record_now(s::SessionEvent::close(kRight, kSink));
    s::SessionEvent const& first = log[0];
    ::fixy::View<s::SessionEvent const> const run = *log.events().segments().begin();
    for (int turn = 0; turn < kSends; ++turn)
        writer.record_now(s::SessionEvent::close(kSink, kLeft));
    CRUCIBLE_TEST_ASSERT(kTest, first.from_role() == kLeft && first.step_id().raw() == 1 && &first == &log[0]);
    CRUCIBLE_TEST_ASSERT(kTest, run.size() == 2 && run.get(1).expect("two events")->from_role() == kRight);
    std::size_t steps = 0;
    for (s::SessionEvent const& event : log.events()) {
        CRUCIBLE_TEST_ASSERT(kTest, event.step_id().raw() == steps + 1);
        writer.record_now(s::SessionEvent::close(kSink, kRight));
        ++steps;
    }
    CRUCIBLE_TEST_ASSERT(kTest, steps == kSends + 2 && log.size() == 2 * steps);
    CRUCIBLE_TEST_ASSERT(kTest, log.back().to_role() == kRight && log.back().step_id().raw() == 2 * steps);
}

// Two threads read one finished log at the same time, by index, by the loop and
// through the merge.  The log is Shareable, so the typed thread door starts the
// threads.  The reads write no state of the log, so ThreadSanitizer sees no
// race.
void two_threads_read_one_finished_log() {
    s::SessionEventLog log{s::SessionTagId{5}};
    {
        s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
        for (int turn = 0; turn < kSends; ++turn)
            writer.record_now(s::SessionEvent::close(kLeft, kSink));
    }
    auto add_steps = [&finished = std::as_const(log)] noexcept {
        std::uint64_t steps = 0;
        for (std::size_t index = 0; index < finished.size(); ++index)
            steps += finished[index].step_id().raw();
        const auto merged = s::merge_session_logs(test_alloc(), finished);
        for (const s::SessionEvent& event : merged.view())
            steps += event.step_id().raw();
        for (const s::SessionEvent& event : finished.events())
            steps += event.step_id().raw();
        return steps + finished.back().step_id().raw();
    };
    const auto sums = ::fixy::thread::run_joined(::fixy::BgLoadCtx{::foundation::effects::testing::bg()},
                                                 ::fixy::thread::ThreadOptions{}, auto{add_steps}, std::move(add_steps))
                          .expect("the test starts two threads");
    constexpr std::uint64_t kLast = kSends;
    constexpr std::uint64_t kSum = 3 * (kLast * (kLast + 1) / 2) + kLast;
    CRUCIBLE_TEST_ASSERT(kTest, sums.at(::fixy::index<0>()) == kSum && sums.at(::fixy::index<1>()) == kSum);
}

// The count of the events that the writer appends beside the reader, and the
// count of the first ones that wait for the reader.
constexpr std::size_t kBeside = 1000;
constexpr std::size_t kHandshakes = 70;

// A thread reads a log while its writer appends to it on the calling thread.
// The reader reads the new events of each prefix, and it merges the log beside
// the writer.  The writer waits for the reader after each of the first appends,
// so the reader reads each of those events from a prefix that has no later
// event.  The wait gives no order from the writer to the reader.  The published
// count of the log is the one such order, and ThreadSanitizer reports each read
// that it does not cover.
void a_thread_reads_a_log_beside_its_writer() {
    s::SessionEventLog log{s::SessionTagId{9}};
    ::foundation::core::Atomic<std::size_t> acknowledged{0};
    auto read = [&log, &acknowledged]() noexcept {
        std::size_t seen = 0;
        while (seen != kBeside) {
            const s::SessionEventLog::events_type events = log.events();
            if (events.size() == seen) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            CRUCIBLE_TEST_ASSERT(kTest, events.size() > seen && events.size() <= kBeside);
            for (std::size_t position = events.size(); position != seen; --position) {
                const s::SessionEvent& event = *events.get(position - 1).expect("the position is below the count");
                CRUCIBLE_TEST_ASSERT(kTest, event.step_id().raw() == position && event.session().value == 9);
                CRUCIBLE_TEST_ASSERT(kTest, event.from_role() == kLeft && event.op() == s::SessionOp::Close);
            }
            seen = events.size();
            CRUCIBLE_TEST_ASSERT(kTest, log.size() >= seen && log[seen - 1].step_id().raw() == seen);
            CRUCIBLE_TEST_ASSERT(kTest, log.back().step_id().raw() >= seen && !log.empty());
            const auto merged = s::merge_session_logs(test_alloc(), log);
            CRUCIBLE_TEST_ASSERT(kTest, merged.size() >= seen && merged.size() <= kBeside);
            std::uint64_t step = 0;
            for (const s::SessionEvent& event : merged.view()) {
                ++step;
                CRUCIBLE_TEST_ASSERT(kTest, event.step_id().raw() == step);
            }
            acknowledged.store_release(seen);
        }
        return seen;
    };
    auto write = [&log, &acknowledged]() noexcept {
        s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
        for (std::size_t count = 1; count <= kBeside; ++count) {
            writer.record_now(s::SessionEvent::close(kLeft, kSink));
            while (count <= kHandshakes && acknowledged.load_acquire() < count)
                CRUCIBLE_SPIN_PAUSE;
        }
        return log.size();
    };
    const auto counts = ::fixy::thread::run_beside(::fixy::BgLoadCtx{::foundation::effects::testing::bg()},
                                                   ::fixy::thread::ThreadOptions{}, std::move(read), std::move(write))
                            .expect("the test starts one thread");
    CRUCIBLE_TEST_ASSERT(kTest, counts.at(::fixy::index<0>()) == kBeside && counts.at(::fixy::index<1>()) == kBeside);
}

// An event whose step goes backward ends the process, and an event with the
// step of the last event goes into the log.
void a_step_that_goes_backward_ends_the_process() {
    const auto event_of_step = [](std::uint64_t step) noexcept {
        return s::SessionEvent::cipher_event(s::SessionOp::StorePending, s::StepId{step}, s::StateHash{},
                                             s::CipherEventOrder{std::uint64_t{1}});
    };
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
    writer.record(event_of_step(5));
    writer.record(event_of_step(5));
    ::fixy::check::expect_dies(
        kTest, [&writer, &event_of_step] noexcept { writer.record(event_of_step(4)); },
        ::fixy::TextView{"foundation: contract violation: keeps_order_(event)"});
    CRUCIBLE_TEST_ASSERT(kTest, log.size() == 2 && log.back().step_id().raw() == 5);
}

// The writer that a call gives back by reference.  The rule use-after-move does
// not read through it, so a test reaches a moved-from writer.
[[nodiscard]] s::SessionEventWriter& through(s::SessionEventWriter& writer) noexcept { return writer; }

// An append through a moved-from writer ends the process, and the writer that
// took its log appends.
void an_append_through_a_moved_writer_ends_the_process() {
    s::SessionEventLog log;
    s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
    s::SessionEventWriter moved{std::move(through(writer))};
    constexpr ::fixy::TextView expected{"foundation: fatal invariant failed: log_ != nullptr"};
    ::fixy::check::expect_dies(
        kTest, [&writer] noexcept { through(writer).record_now(s::SessionEvent::close(kLeft, kSink)); }, expected);
    ::fixy::check::expect_dies(
        kTest, [&writer] noexcept { through(writer).record(s::SessionEvent::close(kLeft, kSink)); }, expected);
    moved.record_now(s::SessionEvent::close(kLeft, kSink));
    CRUCIBLE_TEST_ASSERT(kTest, log.size() == 1);
}

// A read at the count of a log and the last event of an empty log end the
// process.
void a_read_past_the_events_ends_the_process() {
    s::SessionEventLog log;
    const s::SessionEventLog empty;
    s::SessionEventWriter writer = s::mint_session_event_writer(test_alloc(), log);
    writer.record_now(s::SessionEvent::close(kLeft, kSink));
    ::fixy::check::expect_dies(
        kTest, [&log] noexcept { static_cast<void>(log[1]); },
        ::fixy::TextView{"an index of a session event log is below its count"});
    ::fixy::check::expect_dies(
        kTest, [&empty] noexcept { static_cast<void>(empty.back()); },
        ::fixy::TextView{"a session event log whose last event is read holds an event"});
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
    an_append_keeps_each_event_at_its_address();
    two_threads_read_one_finished_log();
    a_thread_reads_a_log_beside_its_writer();
    a_step_that_goes_backward_ends_the_process();
    an_append_through_a_moved_writer_ends_the_process();
    a_read_past_the_events_ends_the_process();
    a_merge_keeps_the_order_of_the_arguments_for_one_step();
    ::fixy::check::pass(kTest, "test_session_event_log_writers: OK\n");
    return 0;
}
