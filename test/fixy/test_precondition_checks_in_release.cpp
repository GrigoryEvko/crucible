// A precondition and a postcondition do their checks in a Release build.
//
// A Release library compiles with NDEBUG and the observe semantic, and this
// translation unit compiles with the same two.  Each test gets -UNDEBUG from
// its factory, and the registration of this test puts -DNDEBUG after it.
// The test proves that NDEBUG does not change CRUCIBLE_PRE or CRUCIBLE_POST.
// A violated condition must stop the process through the violation handler,
// and it must not become an assumption for the optimizer.
//
// fixy::mint_refined is the door that runs the predicate, and its check is
// a CRUCIBLE_PRE.  If that check is only an assumption, a value that fails
// the predicate goes through the checked door as if it were trusted.
//
// Each attack runs in a child process.  The test passes only when the child
// stops by SIGABRT and its standard error names a contract violation.  The
// control runs a good value in the same harness, and it must not abort.

#if !defined(NDEBUG)
#error "Compile this test with NDEBUG.  It proves the Release form of the contract macros."
#endif
#if defined(CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)
#error "Compile this test on a semantic that checks.  The ignore semantic removes the checks by design."
#endif

#include <fixy/Refined.h>
#include <foundation/contracts/Post.h>

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// Volatile, so the optimizer cannot know the value that an attack uses.
int volatile opaque_negative = -1;
int volatile opaque_positive = 7;
int volatile opaque_one = 1;
int volatile sink = 0;

enum class ChildEnd : unsigned char { Aborted, ExitedZero, Other };

struct ChildResult {
    ChildEnd end = ChildEnd::Other;
    std::string error_text;
};

// Runs the body in a child process, and returns how the child stopped and
// what it wrote to standard error.
[[nodiscard]] ChildResult run_in_child(void (*body)()) {
    ChildResult result;
    int channel[2];
    if (::pipe(channel) != 0) return result;
    const ::pid_t child = ::fork();  // SPAWN-PROCESS-OK: a death test observes the abort in a child
    if (child < 0) return result;
    if (child == 0) {
        ::dup2(channel[1], 2);
        ::close(channel[0]);
        body();
        ::_exit(0);
    }
    ::close(channel[1]);
    char buffer[512];
    for (::ssize_t got = ::read(channel[0], buffer, sizeof buffer); got > 0;
         got = ::read(channel[0], buffer, sizeof buffer)) {
        result.error_text.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(channel[0]);
    int status = 0;
    ::waitpid(child, &status, 0);  // SPAWN-PROCESS-OK: the parent reaps the child of the death test
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        result.end = ChildEnd::Aborted;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result.end = ChildEnd::ExitedZero;
    }
    return result;
}

void mint_refined_of_a_negative_value() {
    auto const refined = ::fixy::mint_refined<::fixy::positive>(static_cast<int>(opaque_negative));
    sink = refined.value();
}

void mint_refined_of_a_positive_value() {
    auto const refined = ::fixy::mint_refined<::fixy::positive>(static_cast<int>(opaque_positive));
    sink = refined.value();
}

// The postcondition promises a positive result, and an input of one breaks
// that promise.
[[gnu::noinline]] int decrement_to_positive(int const x) noexcept {
    int const result = x - 1;
    CRUCIBLE_POST(result, result > 0);
    return result;
}

void decrement_one() {
    sink = decrement_to_positive(opaque_one);
}

// Reports one case, and returns true when the child stopped as expected.
[[nodiscard]] bool expect(char const* name, void (*body)(), bool must_abort) {
    const ChildResult result = run_in_child(body);
    const bool names_violation = result.error_text.find("contract violation") != std::string::npos;
    const bool is_expected =
        must_abort ? (result.end == ChildEnd::Aborted && names_violation) : result.end == ChildEnd::ExitedZero;
    if (!is_expected) {
        std::fprintf(stderr, "test_precondition_checks_in_release: %s: the child %s.\nchild stderr:\n%s\n", name,
                     result.end == ChildEnd::Aborted      ? "aborted"
                     : result.end == ChildEnd::ExitedZero ? "continued and exited with status 0"
                                                          : "stopped in a different way",
                     result.error_text.c_str());
    }
    return is_expected;
}

}  // namespace

int main() {
    const bool is_refined_checked =
        expect("mint_refined of a value that fails the predicate", mint_refined_of_a_negative_value, true);
    const bool is_post_checked = expect("CRUCIBLE_POST with a false condition", decrement_one, true);
    const bool is_good_value_admitted =
        expect("mint_refined of a value that passes the predicate", mint_refined_of_a_positive_value, false);
    return is_refined_checked && is_post_checked && is_good_value_admitted ? 0 : 1;
}
