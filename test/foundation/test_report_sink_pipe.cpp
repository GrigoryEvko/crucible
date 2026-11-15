// The sink write of the Report family, with a standard output or a standard
// error that is a pipe with no reader.
//
// A write to a pipe whose reader has gone raises SIGPIPE in the thread, and
// the default action ends the process.  A report must not end the process by
// that signal.  So a report to each stream and a fatal exit give no SIGPIPE
// to the thread, and the mask of the thread after the report is the mask
// before it.  A thread that blocked SIGPIPE before the report keeps its mask,
// and the signal of the write stays pending for it.
//
// Each case puts a handler on SIGPIPE that marks the arrival of the signal, so
// a write that raises the signal fails the case and does not end the process.
// The runner of the tests can give the process SIG_IGN for SIGPIPE and a mask
// that blocks it, and each of the two hides the signal.  So each case sets the
// action and the mask that it reads.  A child process with the default action
// gives the same proof, but the quarantine plugin refuses a fork in a test.

#include <foundation/Quarantine.h>
#include <foundation/core/Report.h>

#include "abort_probe.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a report to a stream with no reader raises no SIGPIPE in its thread")
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

namespace {

// One when a SIGPIPE reached mark_pipe_signal.
volatile sig_atomic_t pipe_signal_arrived = 0;

// The action of SIGPIPE and the mask of the thread that install_pipe_marker
// replaced, and the descriptor that holds the broken stream during a case.
struct ::sigaction replaced_pipe_action{};
::sigset_t replaced_mask{};
int saved_stream = -1;

void mark_pipe_signal(int) noexcept { pipe_signal_arrived = 1; }

[[nodiscard]] ::sigset_t pipe_signal_set() noexcept {
    ::sigset_t pipe{};
    static_cast<void>(::sigemptyset(&pipe));
    static_cast<void>(::sigaddset(&pipe, SIGPIPE));
    return pipe;
}

// Puts mark_pipe_signal on SIGPIPE.  The mask of the thread then blocks
// SIGPIPE when SHOULD_BLOCK is true, and does not block it otherwise.
[[nodiscard]] bool install_pipe_marker(bool should_block) noexcept {
    pipe_signal_arrived = 0;
    struct ::sigaction marker{};
    marker.sa_handler = &mark_pipe_signal;
    static_cast<void>(::sigemptyset(&marker.sa_mask));
    ::sigset_t const pipe = pipe_signal_set();
    return ::sigaction(SIGPIPE, &marker, &replaced_pipe_action) == 0
        && ::pthread_sigmask(should_block ? SIG_BLOCK : SIG_UNBLOCK, &pipe, &replaced_mask) == 0;
}

// Takes a pending SIGPIPE of the thread with a wait of zero, and then puts
// back the mask and the action that install_pipe_marker replaced.
[[nodiscard]] bool restore_pipe_action() noexcept {
    ::sigset_t const pipe = pipe_signal_set();
    ::timespec const no_wait{};
    static_cast<void>(::sigtimedwait(&pipe, nullptr, &no_wait));
    return ::pthread_sigmask(SIG_SETMASK, &replaced_mask, nullptr) == 0
        && ::sigaction(SIGPIPE, &replaced_pipe_action, nullptr) == 0;
}

[[nodiscard]] bool has_pipe_signal_arrived() noexcept { return pipe_signal_arrived != 0; }

// True when the mask of the thread blocks SIGPIPE.
[[nodiscard]] bool is_pipe_signal_blocked() noexcept {
    ::sigset_t current{};
    return ::pthread_sigmask(SIG_SETMASK, nullptr, &current) == 0 && ::sigismember(&current, SIGPIPE) == 1;
}

// The mask of the thread.
[[nodiscard]] ::sigset_t mask_of_thread() noexcept {
    ::sigset_t current{};
    static_cast<void>(::pthread_sigmask(SIG_SETMASK, nullptr, &current));
    return current;
}

// The mask of the thread before the body of a case.
::sigset_t mask_before_body{};

void record_mask_before_body() noexcept { mask_before_body = mask_of_thread(); }

// True when the mask of the thread holds the signals of the mask before the
// body, from 1 to 64, and no other signal.
[[nodiscard]] bool is_mask_kept() noexcept {
    ::sigset_t const current = mask_of_thread();
    for (int signal_number = 1; signal_number <= 64; ++signal_number) {
        if (::sigismember(&mask_before_body, signal_number) != ::sigismember(&current, signal_number)) return false;
    }
    return true;
}

// True when a SIGPIPE waits for the thread or for the process.
[[nodiscard]] bool is_pipe_signal_pending() noexcept {
    ::sigset_t pending{};
    return ::sigpending(&pending) == 0 && ::sigismember(&pending, SIGPIPE) == 1;
}

// The descriptor of the stream of the sink SINK.
[[nodiscard]] int stream_of(::foundation::core::Sink sink) noexcept {
    return sink == ::foundation::core::Sink::Out ? STDOUT_FILENO : STDERR_FILENO;
}

// Makes the stream STREAM the write end of a pipe whose read end is closed.
[[nodiscard]] bool break_stream(int stream) noexcept {
    int ends[2] = {-1, -1};
    if (::pipe2(ends, O_CLOEXEC) != 0) return false;
    static_cast<void>(::close(ends[0]));
    saved_stream = ::fcntl(stream, F_DUPFD_CLOEXEC, 3);
    bool const is_broken = saved_stream >= 0 && ::dup2(ends[1], stream) == stream;
    static_cast<void>(::close(ends[1]));
    return is_broken;
}

// Puts back the stream STREAM that break_stream replaced.
[[nodiscard]] bool repair_stream(int stream) noexcept {
    if (saved_stream < 0) return false;
    bool const is_repaired = ::dup2(saved_stream, stream) == stream;
    static_cast<void>(::close(saved_stream));
    saved_stream = -1;
    return is_repaired;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace core = ::foundation::core;

// What a report did to the signal state of its thread.
struct PipeOutcome {
    bool is_prepared = false;
    bool has_signal_arrived = false;
    bool is_blocked_after = false;
    bool is_mask_kept = false;
    bool is_pending_after = false;
    bool is_restored = false;
};

// Runs BODY with the marker on SIGPIPE, the mask that SHOULD_BLOCK gives, and
// no reader on the stream of SINK.  The outcome reads the mask and the pending
// signals of the thread after BODY, before the case puts its state back.
template <class Body>
[[nodiscard]] PipeOutcome run_with_broken_pipe(core::Sink sink, bool should_block, Body const& body) noexcept {
    PipeOutcome outcome{};
    int const stream = stream_of(sink);
    if (!install_pipe_marker(should_block)) return outcome;
    if (!break_stream(stream)) {
        static_cast<void>(repair_stream(stream));
        static_cast<void>(restore_pipe_action());
        return outcome;
    }
    outcome.is_prepared = true;
    record_mask_before_body();
    body();
    outcome.is_blocked_after = is_pipe_signal_blocked();
    outcome.is_mask_kept = is_mask_kept();
    outcome.is_pending_after = is_pipe_signal_pending();
    bool const is_repaired = repair_stream(stream);
    outcome.is_restored = restore_pipe_action() && is_repaired;
    outcome.has_signal_arrived = has_pipe_signal_arrived();
    return outcome;
}

// A report to standard error with no reader raises no SIGPIPE, and the thread
// keeps its mask, which does not block SIGPIPE, and no pending SIGPIPE.
[[nodiscard]] int check_report_raises_no_signal() noexcept {
    PipeOutcome const outcome = run_with_broken_pipe(core::Sink::Err, false, [] {
        core::report(core::Sink::Err, "test_report_sink_pipe: a report that no reader reads\n");
    });
    if (!outcome.is_prepared) return 1;
    if (outcome.has_signal_arrived) return 2;
    if (outcome.is_blocked_after) return 3;
    if (!outcome.is_mask_kept) return 4;
    if (outcome.is_pending_after) return 5;
    if (!outcome.is_restored) return 6;
    return 0;
}

// A report to standard output with no reader raises no SIGPIPE either, so a
// program whose reader closed standard output continues to its own exit.  The
// thread keeps its mask and no pending SIGPIPE.
[[nodiscard]] int check_output_report_raises_no_signal() noexcept {
    PipeOutcome const outcome = run_with_broken_pipe(core::Sink::Out, false, [] {
        core::report(core::Sink::Out, "test_report_sink_pipe: an output that no reader reads\n");
    });
    if (!outcome.is_prepared) return 31;
    if (outcome.has_signal_arrived) return 32;
    if (outcome.is_blocked_after) return 33;
    if (!outcome.is_mask_kept) return 34;
    if (outcome.is_pending_after) return 35;
    if (!outcome.is_restored) return 36;
    return 0;
}

// A fatal exit writes its text to standard error with no reader, and it ends
// the process by SIGABRT and not by SIGPIPE.
[[nodiscard]] int check_fatal_raises_no_signal() noexcept {
    bool has_aborted = false;
    PipeOutcome const outcome = run_with_broken_pipe(core::Sink::Err, false, [&has_aborted] {
        has_aborted =
            ::crucible::test::aborts([] { core::fatal("test_report_sink_pipe: a fatal exit that no reader reads"); });
    });
    if (!outcome.is_prepared) return 11;
    if (!has_aborted) return 12;
    if (outcome.has_signal_arrived) return 13;
    if (outcome.is_blocked_after) return 14;
    if (!outcome.is_mask_kept) return 15;
    if (outcome.is_pending_after) return 16;
    if (!outcome.is_restored) return 17;
    return 0;
}

// A thread that blocked SIGPIPE before the report keeps its mask, and the
// SIGPIPE of the write stays pending for it.  The case takes the signal
// before it unblocks SIGPIPE, so the handler marks no arrival.
[[nodiscard]] int check_blocked_thread_keeps_its_signal() noexcept {
    PipeOutcome const outcome = run_with_broken_pipe(core::Sink::Err, true, [] {
        core::report(core::Sink::Err, "test_report_sink_pipe: a report of a thread that blocks SIGPIPE\n");
    });
    if (!outcome.is_prepared) return 21;
    if (!outcome.is_blocked_after) return 22;
    if (!outcome.is_mask_kept) return 23;
    if (!outcome.is_pending_after) return 24;
    if (outcome.has_signal_arrived) return 25;
    if (!outcome.is_restored) return 26;
    return 0;
}

}  // namespace

int main() {
    int failed = check_report_raises_no_signal();
    if (failed == 0) failed = check_output_report_raises_no_signal();
    if (failed == 0) failed = check_fatal_raises_no_signal();
    if (failed == 0) failed = check_blocked_thread_keeps_its_signal();
    if (failed != 0) {
        core::report(core::Sink::Err, "test_report_sink_pipe: check {} failed\n", failed);
        return failed;
    }
    core::report(core::Sink::Out, "test_report_sink_pipe: passed\n");
    return 0;
}
