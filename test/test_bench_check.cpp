// A setup check of a bench fires in each build.
//
// The presets that build the benches (bench, pgo-generate and pgo) define
// NDEBUG, so assert checks nothing in a bench.  This file defines NDEBUG
// before each include, as those builds do, and a failed CRUCIBLE_BENCH_CHECK
// must still stop the process with SIGABRT.  Its message gives the file, the
// line and the condition.
#define NDEBUG

#include "bench_harness.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// The report of one child process: whether it ended on SIGABRT, and its
// standard error.
struct ChildResult {
    bool was_aborted = false;
    char report[4096]{};
};

// Runs body in a child process, with the standard error of the child on a
// pipe.  The child ends with exit status 0 when the body returns.
ChildResult run_in_child(void (*body)()) {
    ChildResult result{};
    int pipe_ends[2] = {-1, -1};
    if (::pipe(pipe_ends) != 0) std::abort();
    std::fflush(stderr);
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, the failed check ends the process
    if (pid < 0) std::abort();
    if (pid == 0) {
        ::close(pipe_ends[0]);
        if (::dup2(pipe_ends[1], STDERR_FILENO) < 0) std::_Exit(3);
        body();
        std::_Exit(0);
    }
    ::close(pipe_ends[1]);
    std::size_t used = 0;
    while (used + 1 < sizeof(result.report)) {
        const ssize_t count = ::read(pipe_ends[0], result.report + used, sizeof(result.report) - 1 - used);
        if (count > 0) {
            used += static_cast<std::size_t>(count);
        } else if (count == 0 || errno != EINTR) {
            break;
        }
    }
    ::close(pipe_ends[0]);
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) std::abort();  // SPAWN-PROCESS-OK: reaps the child forked above
    result.was_aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    return result;
}

// The value comes from the environment, so the compiler cannot fold the
// check away.
void fail_a_setup_check() {
    const int setup_value = (std::getenv("CRUCIBLE_BENCH_CHECK_NEVER_SET") == nullptr) ? 2 : 3;
    CRUCIBLE_BENCH_CHECK(setup_value == 3);
}

void pass_a_setup_check() {
    const int setup_value = (std::getenv("CRUCIBLE_BENCH_CHECK_NEVER_SET") == nullptr) ? 2 : 3;
    CRUCIBLE_BENCH_CHECK(setup_value == 2);
}

[[noreturn]] void fail_test(const char* reason, const ChildResult& child) {
    std::fprintf(stderr, "test_bench_check: FAILED: %s\n  child stderr: %s\n", reason, child.report);
    std::abort();
}

}  // namespace

int main() {
    const ChildResult failed = run_in_child(&fail_a_setup_check);
    if (!failed.was_aborted) fail_test("a failed bench check under NDEBUG did not abort", failed);
    if (std::strstr(failed.report, "test_bench_check.cpp:") == nullptr)
        fail_test("the message of a failed bench check does not name its file", failed);
    if (std::strstr(failed.report, "setup_value == 3") == nullptr)
        fail_test("the message of a failed bench check does not give its condition", failed);

    const ChildResult passed = run_in_child(&pass_a_setup_check);
    if (passed.was_aborted) fail_test("a bench check that holds aborted", passed);

    std::printf("test_bench_check: a failed check aborts under NDEBUG, and a check that holds does not\n");
    return 0;
}
