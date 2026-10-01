// A reserve of the expression pool past the capacity bound of its table is
// refused before the table is allocated, under each contract semantic.  The
// build compiles this file twice: one time under the contract semantic of the
// preset, and one time with CRUCIBLE_CONTRACT_IGNORE_OPTIONS.  When a contract
// assertion checks, the assertion of reserve refuses the request.  When no
// contract assertion checks, the always-on check of SwissTableBuffer::allocate
// refuses it.  A CRUCIBLE_PRE of the same capacity before that check gives the
// optimizer an assumption under the ignore semantic, and the optimizer then
// deletes the bound of the check.
//
// Each refusal ends the process, so each attack runs in a child process.  The
// parent reads the standard error of the child, and it requires SIGABRT and a
// report that names one of the two checks.  An abort for a failed allocation
// names neither.

#include <crucible/ExprPool.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace eff = ::foundation::effects;

// The largest reserve: n * 8 <= capacity * 7 at the capacity bound 1 << 30.
constexpr std::size_t kLargestReserve = ((std::size_t{1} << 30) * 7) / 8;

// The text of the contract assertion of reserve, and the text of the
// always-on check of the table buffer.
constexpr std::string_view kContractRefusal = "n_entries";
constexpr std::string_view kBufferRefusal = "is_swiss_table_capacity";

struct ChildResult {
    bool was_aborted = false;
    char report[8192]{};
};

// Runs attack in a child process, with the standard error of the child on a
// pipe.  The child ends with exit status 0 when the attack returns.  Without
// a sanitizer, the child also gets an address-space limit of 8 GiB, so an
// allocation of the table past the bound fails at once and does not fill the
// memory of the host.
ChildResult run_in_child(void (*attack)()) {
    ChildResult result{};
    int pipe_ends[2] = {-1, -1};
    if (::pipe(pipe_ends) != 0) std::abort();
    std::fflush(stderr);
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, the refusal ends the process
    if (pid < 0) std::abort();
    if (pid == 0) {
        ::close(pipe_ends[0]);
        if (::dup2(pipe_ends[1], STDERR_FILENO) < 0) std::_Exit(3);
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
        const rlimit limit{.rlim_cur = rlim_t{8} << 30, .rlim_max = rlim_t{8} << 30};
        if (::setrlimit(RLIMIT_AS, &limit) != 0) std::_Exit(4);
#endif
        attack();
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

[[nodiscard]] bool is_refused(void (*attack)()) {
    const ChildResult result = run_in_child(attack);
    const std::string_view report{result.report};
    const bool names_the_check = report.find(kContractRefusal) != std::string_view::npos
                              || report.find(kBufferRefusal) != std::string_view::npos;
    if (!result.was_aborted || !names_the_check) {
        std::fprintf(stderr, "child: aborted=%d report:\n%s\n", result.was_aborted ? 1 : 0, result.report);
    }
    return result.was_aborted && names_the_check;
}

void reserve_entries(std::size_t entries) {
    auto test = eff::testing::test();
    crucible::ExprPool pool{test.alloc};
    pool.reserve(entries);
}

// A reserve at the bound passes the check.  This shows that the attack below
// fails for the size of the request and for no other reason.  The table of
// the bound takes 9 GiB of address space, so the reserve here is smaller: it
// grows the table to 1 << 18 slots.
void test_reserve_inside_the_bound_passes() {
    reserve_entries(kLargestReserve / (std::size_t{1} << 12));
    std::printf("  test_reserve_inside_the_bound_passes: PASSED\n");
}

// One entry more than the bound needs a table of 1 << 31 slots.
void test_reserve_past_the_bound_is_refused() {
    assert(is_refused([] { reserve_entries(kLargestReserve + 1); }));
    std::printf("  test_reserve_past_the_bound_is_refused: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_expr_pool_capacity:\n");
    test_reserve_inside_the_bound_passes();
    test_reserve_past_the_bound_is_refused();
    std::printf("test_expr_pool_capacity: all tests passed\n");
    return 0;
}
