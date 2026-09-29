// The contract handler of src/foundation/ContractHandler.cpp, in child
// processes.
//
// A report names the violated condition, its position and the note of a
// message form, and the child ends by SIGABRT.
//
// A second violation on the thread that reports the first one aborts at
// once.  The child fills the pipe that is its standard error, so the report
// of the first violation blocks in write(2).  A timer signal then raises a
// second violation in its handler, on the same thread.  A handler that
// reports the second violation blocks on the full pipe too, or it recurses
// through the lock of the stream and the heap.  So the child must end by
// SIGABRT before the parent gives up on it.

#include <foundation/contracts/Pre.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

// A volatile condition, so the compiler cannot fold the clause to false.
volatile bool g_condition_holds = false;

[[gnu::noinline]] void violate_a_clause() noexcept { contract_assert(g_condition_holds); }

[[gnu::noinline]] void violate_with_a_note() noexcept {
    CRUCIBLE_PRE_MSG(g_condition_holds, "the note of the message form");
}

void violate_a_clause_in_a_signal(int /*signal*/) { violate_a_clause(); }

void violate_with_a_note_in_a_signal(int /*signal*/) { violate_with_a_note(); }

struct ChildResult {
    bool ended_by_sigabrt = false;
    bool ended_in_time = false;
    std::string standard_error;
};

// Fills the pipe on file descriptor 2, so that the next write to it blocks.
void fill_standard_error() noexcept {
    const int flags = ::fcntl(STDERR_FILENO, F_GETFL);
    if (flags < 0 || ::fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) ::_exit(90);
    const char filler = 'f';
    while (::write(STDERR_FILENO, &filler, 1) == 1 || errno == EINTR) {}
    if (errno != EAGAIN || ::fcntl(STDERR_FILENO, F_SETFL, flags) != 0) ::_exit(91);
}

// Runs body in a child whose standard error is a pipe.  The parent waits at
// most ten seconds for the child, and it reads the pipe only after the child
// ends, so a child that fills the pipe keeps it full.
template <typename Body>
[[nodiscard]] ChildResult run_in_child(Body body) {
    ChildResult result;
    int channel[2];
    if (::pipe(channel) != 0) return result;
    const ::pid_t child = ::fork();  // SPAWN-PROCESS-OK: a death test observes the abort in a child
    if (child < 0) return result;
    if (child == 0) {
        ::dup2(channel[1], STDERR_FILENO);
        ::close(channel[0]);
        ::close(channel[1]);
        body();
        ::_exit(0);
    }
    ::close(channel[1]);
    int status = 0;
    const ::timespec pause{0, 10000000};
    for (int tick = 0; tick < 1000; ++tick) {
        const ::pid_t reaped = ::waitpid(child, &status, WNOHANG);  // SPAWN-PROCESS-OK: the death test reaps its child
        if (reaped == child) {
            result.ended_in_time = true;
            break;
        }
        ::nanosleep(&pause, nullptr);
    }
    if (!result.ended_in_time) {
        ::kill(child, SIGKILL);
        ::waitpid(child, &status, 0);  // SPAWN-PROCESS-OK: the death test reaps its child
    }
    result.ended_by_sigabrt = result.ended_in_time && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    char buffer[512];
    for (::ssize_t got = ::read(channel[0], buffer, sizeof buffer); got > 0;
         got = ::read(channel[0], buffer, sizeof buffer)) {
        result.standard_error.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(channel[0]);
    return result;
}

[[nodiscard]] bool holds(bool condition, const char* what, ChildResult const& result) {
    if (!condition) {
        std::fprintf(stderr, "test_contract_handler: FAIL: %s (sigabrt=%d, in time=%d)\n", what,
                     result.ended_by_sigabrt ? 1 : 0, result.ended_in_time ? 1 : 0);
        // A child that filled the pipe leaves 64 KiB of filler bytes, so
        // only the tail of its output is printed.
        const std::size_t shown = result.standard_error.size() < 2048 ? result.standard_error.size() : 2048;
        std::fprintf(stderr, "child output (last %zu bytes):\n%s\n", shown,
                     result.standard_error.substr(result.standard_error.size() - shown).c_str());
    }
    return condition;
}

[[nodiscard]] bool clause_report_names_the_condition() {
    const ChildResult result = run_in_child([] { violate_a_clause(); });
    const std::string_view text = result.standard_error;
    return holds(result.ended_by_sigabrt && text.starts_with("foundation: contract violation: g_condition_holds\n")
                     && text.find("  at ") != std::string_view::npos
                     && text.find("test_contract_handler.cpp:") != std::string_view::npos
                     && text.find("violate_a_clause") != std::string_view::npos
                     && text.find("note:") == std::string_view::npos,
                 "a violated clause is reported with its condition and position", result);
}

[[nodiscard]] bool message_report_carries_its_note() {
    const ChildResult result = run_in_child([] { violate_with_a_note(); });
    const std::string_view text = result.standard_error;
    return holds(result.ended_by_sigabrt && text.starts_with("foundation: contract violation: g_condition_holds\n")
                     && text.find("violate_with_a_note") != std::string_view::npos
                     && text.find("\n  note: the note of the message form\n") != std::string_view::npos,
                 "a violated message form is reported with its note", result);
}

// The first violation blocks in its report, and the timer raises the second
// one in a signal handler on the same thread.
template <void (*First)() noexcept, void (*Second)(int)>
[[nodiscard]] bool second_violation_aborts_at_once(const char* what) {
    const ChildResult result = run_in_child([] {
        fill_standard_error();
        struct sigaction action{};
        action.sa_handler = Second;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        if (::sigaction(SIGALRM, &action, nullptr) != 0) ::_exit(92);
        ::alarm(1);
        First();
    });
    return holds(result.ended_by_sigabrt, what, result);
}

}  // namespace

int main() {
    bool passed = clause_report_names_the_condition();
    passed = message_report_carries_its_note() && passed;
    passed = second_violation_aborts_at_once<violate_a_clause, violate_a_clause_in_a_signal>(
                 "a second clause violation during a blocked report aborts at once")
          && passed;
    passed = second_violation_aborts_at_once<violate_with_a_note, violate_a_clause_in_a_signal>(
                 "a clause violation during a blocked message report aborts at once")
          && passed;
    passed = second_violation_aborts_at_once<violate_a_clause, violate_with_a_note_in_a_signal>(
                 "a message violation during a blocked clause report aborts at once")
          && passed;
    std::printf("test_contract_handler: %s\n", passed ? "passed" : "FAILED");
    return passed ? 0 : 1;
}
