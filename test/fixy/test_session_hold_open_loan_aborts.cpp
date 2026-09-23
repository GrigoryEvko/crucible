// A borrower that drops its hold without a release aborts with a named
// diagnostic.  Without the check, the lender waits for a release that
// never comes, which is the silent drop that LinearActris (Jacobs,
// Hinrichsen, Krebbers, POPL 2024) forbids.  The attack runs in a child
// process, and the test passes only on that diagnostic.

#include <fixy/session/Payload.h>

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// Runs the attack in a child process.  The test passes only when the
// child ends by SIGABRT and its standard error names the rule, so a
// different crash, or no crash, fails it.
[[nodiscard]] bool attack_aborts_with(void (*attack)(), const char* expected) {
    int channel[2];
    if (::pipe(channel) != 0) return false;
    // The child runs the attack, so that its abort is the result the test reads.
    const ::pid_t child = ::fork();  // SPAWN-PROCESS-OK: a death test observes the abort in a child
    if (child < 0) return false;
    if (child == 0) {
        ::dup2(channel[1], 2);
        ::close(channel[0]);
        attack();
        ::_exit(0);
    }
    ::close(channel[1]);
    std::string text;
    char buffer[512];
    for (::ssize_t got = ::read(channel[0], buffer, sizeof buffer); got > 0;
         got = ::read(channel[0], buffer, sizeof buffer)) {
        text.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(channel[0]);
    int status = 0;
    ::waitpid(child, &status, 0);  // SPAWN-PROCESS-OK: the parent reaps the child of the death test
    const bool aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    const bool named = text.find(expected) != std::string::npos;
    if (!aborted || !named) std::fprintf(stderr, "child output:\n%s\n", text.c_str());
    return aborted && named;
}

void drop_open_loan() {
    namespace fp = ::foundation::permissions;
    namespace sess = ::fixy::session;
    auto lender = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto [loan, lent] = std::move(lender).lend<Region>(1);
    auto [value, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    (void)value;
    {
        auto dropped = std::move(borrowing);
    }
}

}  // namespace

int main() {
    const bool refused = attack_aborts_with(drop_open_loan, "[Hold_Open_Loan]: a PermHold was destroyed with a loan open");
    std::printf("test_session_hold_open_loan_aborts: %s\n", refused ? "refused" : "NOT REFUSED");
    return refused ? 0 : 1;
}
