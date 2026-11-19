// The contract handler of src/foundation/ContractHandler.cpp, in child
// processes.
//
// A report names the violated condition and its position, and the child
// ends by SIGABRT.  The predicate text of a message form holds its message.
// These two cases run through the child door of fixy/os/Process.h, and the
// test reads the report of the child.
//
// A second violation on the thread that reports the first one aborts at
// once.  The child fills the pipe that is its standard error, so the report
// of the first violation blocks in write(2).  A timer signal then raises a
// second violation in its handler, on the same thread.  A handler that
// reports the second violation blocks on the full pipe too, or it recurses
// through the lock of the stream and the heap.  So the child must end by
// SIGABRT before the parent gives up on it.
//
// The wait with a limit of the child door reads no output while the child
// runs, so the pipe stays full.  It ends a child that runs past its limit,
// and its report tells that.  The child fills its pipe and arms the timer
// with raw calls, in a PROBE region.
//
// The timer ticks every 20 ms.  A tick before the child calls its first
// violation raises nothing.  A child that waits for a CPU gets its tick
// later, never earlier.  A tick can still land after that call and before
// the handler marks the thread.  Then the violation of the tick reports
// first, and it blocks inside the signal handler.  The handler runs with
// SA_NODEFER, so the next tick still arrives, and its violation is the
// second one on the thread.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include <foundation/Quarantine.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Abort.h>

#include <cstddef>
#include <utility>

// The first token of the condition of violate_after_a_far_macro.  The
// predicate text of a condition that starts with a macro of this file runs
// from this definition to the message near the end of the file.  So the text
// is longer than the 4096 characters that a report keeps of one string.
#define FIRST_TOKEN_OF_A_FAR_MACRO g_condition_holds

namespace {

// True when the test runs under the emulator of a cross build, qemu user mode
// (test/foundation/CMakeLists.txt sets the definition).
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

// A volatile condition, so the compiler cannot fold the clause to false.
volatile bool g_condition_holds = false;

[[gnu::noinline]] void violate_a_clause() noexcept { contract_assert(g_condition_holds); }

[[gnu::noinline]] void violate_with_a_message() noexcept {
    CRUCIBLE_PRE_MSG(g_condition_holds, "the message of the message form");
}

// Runs body in a child through the child door, and gives its report.
template <typename Body>
[[nodiscard]] ::fixy::process::ChildReport run_through_the_door(::fixy::TestRunnerCtx const& test, Body&& body) {
    ::fixy::process::Child child =
        ::fixy::process::spawn_child(test, std::forward<Body>(body)).expect("the test starts a child");
    return ::fixy::process::wait(std::move(child)).expect("the test waits for its child");
}

// Writes the output of a child whose report the test refuses.
void show_the_child_output(::fixy::process::ChildReport const& report) {
    ::fixy::report(::fixy::Sink::Err, "child output:\n{}\n", report.output());
}

void clause_report_names_the_condition(::fixy::TestRunnerCtx const& test) {
    ::fixy::process::ChildReport const report = run_through_the_door(test, [] noexcept { violate_a_clause(); });
    ::fixy::TextView const text = report.output();
    bool const is_reported =
        report.is_signaled_by(::foundation::os::kAbortSignal)
        && ::fixy::starts_with(text, ::fixy::TextView{"foundation: contract violation: g_condition_holds\n"})
        && ::fixy::find(text, ::fixy::TextView{"  at "}).is_some()
        && ::fixy::find(text, ::fixy::TextView{"test_contract_handler.cpp:"}).is_some()
        && ::fixy::find(text, ::fixy::TextView{"violate_a_clause"}).is_some()
        && ::fixy::find(text, ::fixy::TextView{"note:"}).is_none();
    if (!is_reported) show_the_child_output(report);
    CRUCIBLE_TEST_ASSERT(test, is_reported);
    ::fixy::check::pass(test, "  a violated clause is reported with its condition and position\n");
}

// The message is a part of the predicate text, and the report has no other
// line that can hold it.  So the report starts with the condition, and it
// holds the message.
void message_report_carries_its_message(::fixy::TestRunnerCtx const& test) {
    ::fixy::process::ChildReport const report = run_through_the_door(test, [] noexcept { violate_with_a_message(); });
    ::fixy::TextView const text = report.output();
    bool const is_reported =
        report.is_signaled_by(::foundation::os::kAbortSignal)
        && ::fixy::starts_with(text, ::fixy::TextView{"foundation: contract violation: g_condition_holds"})
        && ::fixy::find(text, ::fixy::TextView{"violate_with_a_message"}).is_some()
        && ::fixy::find(text, ::fixy::TextView{"the message of the message form"}).is_some();
    if (!is_reported) show_the_child_output(report);
    CRUCIBLE_TEST_ASSERT(test, is_reported);
    ::fixy::check::pass(test, "  a violated message form is reported with its message\n");
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a violation report blocks on a full pipe, and a timer raises a second one")
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

namespace {

// The longest time that the parent waits for a child whose report blocks.
// A real kernel ends the child by SIGABRT at the first tick of its timer.
constexpr ::fixy::process::WaitLimit kBlockedReportLimit{10000};

// The count of the last bytes of the output of a refused child that the test
// writes.  A child that filled the pipe leaves 64 KiB of filler bytes.
constexpr std::size_t kShownOutputBytes = 2048;

// The child sets this immediately before its first violation.
volatile std::sig_atomic_t g_first_violation_started = 0;

void violate_a_clause_in_a_signal(int /*signal*/) {
    if (g_first_violation_started != 0) violate_a_clause();
}

void violate_with_a_message_in_a_signal(int /*signal*/) {
    if (g_first_violation_started != 0) violate_with_a_message();
}

// Fills the pipe on file descriptor 2, so that the next write to it blocks.
void fill_standard_error() noexcept {
    const int flags = ::fcntl(STDERR_FILENO, F_GETFL);
    if (flags < 0 || ::fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) ::_exit(90);
    const char filler = 'f';
    while (::write(STDERR_FILENO, &filler, 1) == 1 || errno == EINTR) {}
    if (errno != EAGAIN || ::fcntl(STDERR_FILENO, F_SETFL, flags) != 0) ::_exit(91);
}

// True when the child ended by SIGABRT before the limit of its wait.  A
// refused child gets its end and the last bytes of its output written.
[[nodiscard]] bool holds(::fixy::process::ChildReport const& report, ::fixy::TextView what) {
    bool const is_aborted = report.is_signaled_by(::foundation::os::kAbortSignal) && !report.is_ended_at_limit();
    if (!is_aborted) {
        ::fixy::TextView const output = report.output();
        std::size_t const shown = output.size() < kShownOutputBytes ? output.size() : kShownOutputBytes;
        ::fixy::report(
            ::fixy::Sink::Err,
            "test_contract_handler: FAIL: {} (sigabrt={}, at the limit={})\nchild output (last {} bytes):\n{}\n", what,
            report.is_signaled_by(::foundation::os::kAbortSignal), report.is_ended_at_limit(), shown,
            output.window(output.size() - shown, shown).expect("the tail is a part of the output"));
    }
    return is_aborted;
}

// The first violation blocks in its report, and a tick of the timer raises
// the second one in a signal handler on the same thread.  The child door
// starts the child, and its wait with a limit keeps the pipe full.
template <void (*First)() noexcept, void (*Second)(int)>
[[nodiscard]] bool second_violation_aborts_at_once(::fixy::TestRunnerCtx const& test, ::fixy::TextView what) {
    ::fixy::process::Child child = ::fixy::process::spawn_child(test, [] noexcept {
                                       fill_standard_error();
                                       struct sigaction action{};
                                       action.sa_handler = Second;
                                       sigemptyset(&action.sa_mask);
                                       action.sa_flags = SA_NODEFER;
                                       if (::sigaction(SIGALRM, &action, nullptr) != 0) ::_exit(92);
                                       const ::itimerval every_20_ms{{0, 20000}, {0, 20000}};
                                       if (::setitimer(ITIMER_REAL, &every_20_ms, nullptr) != 0) ::_exit(93);
                                       g_first_violation_started = 1;
                                       First();
                                   }).expect("the test starts a child");
    ::fixy::process::ChildReport const report =
        ::fixy::process::wait_within(std::move(child), kBlockedReportLimit).expect("the test waits for its child");
    return holds(report, what);
}

[[nodiscard]] bool second_clause_violation_aborts(::fixy::TestRunnerCtx const& test) {
    return second_violation_aborts_at_once<violate_a_clause, violate_a_clause_in_a_signal>(
        test, ::fixy::TextView{"a second clause violation during a blocked report aborts at once"});
}

[[nodiscard]] bool clause_violation_during_a_message_report_aborts(::fixy::TestRunnerCtx const& test) {
    return second_violation_aborts_at_once<violate_with_a_message, violate_a_clause_in_a_signal>(
        test, ::fixy::TextView{"a clause violation during a blocked message report aborts at once"});
}

[[nodiscard]] bool message_violation_during_a_clause_report_aborts(::fixy::TestRunnerCtx const& test) {
    return second_violation_aborts_at_once<violate_a_clause, violate_with_a_message_in_a_signal>(
        test, ::fixy::TextView{"a message violation during a blocked clause report aborts at once"});
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

[[gnu::noinline]] void violate_after_a_far_macro() noexcept {
    CRUCIBLE_PRE_MSG(FIRST_TOKEN_OF_A_FAR_MACRO, "the message after a far macro");
}

// The report of a predicate text of more than 4096 characters keeps the
// start and the end of the text, with a mark of the cut, so it holds the
// message.  The mark shows that the text is longer than the bound, so the
// case still tests a cut when the file changes.
void long_report_keeps_its_message(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test, [] noexcept { violate_after_a_far_macro(); }, ::fixy::TextView{"the message after a far macro"});
    ::fixy::check::expect_dies(
        test, [] noexcept { violate_after_a_far_macro(); }, ::fixy::TextView{" characters cut ...] "});
    ::fixy::check::pass(test, "  a long predicate text is reported with its message\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    clause_report_names_the_condition(test);
    message_report_carries_its_message(test);
    long_report_keeps_its_message(test);
    // qemu user mode writes its own line of an uncaught signal of the guest to
    // the standard error of the child before it ends.  That standard error is
    // the full pipe of a blocked report, so qemu blocks in the write, and the
    // child does not end in time.  A real kernel ends the child by SIGABRT at
    // once.
    if (kIsUnderEmulator) {
        ::fixy::check::pass(test,
                            "  the three cases of a blocked report SKIPPED (qemu-user blocks on the full pipe)\n");
    } else {
        CRUCIBLE_TEST_ASSERT(test, second_clause_violation_aborts(test));
        CRUCIBLE_TEST_ASSERT(test, clause_violation_during_a_message_report_aborts(test));
        CRUCIBLE_TEST_ASSERT(test, message_violation_during_a_clause_report_aborts(test));
    }
    ::fixy::check::pass(test, "test_contract_handler: passed\n");
    return 0;
}
