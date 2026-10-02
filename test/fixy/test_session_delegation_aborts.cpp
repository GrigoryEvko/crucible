// Three misuses of a DelegatedSession abort with a named diagnostic.
//
//   1. An accept on a DelegatedSession that a move emptied.  A second
//      accept would build a second handle over a Resource that moved.
//   2. A delegation with a hold that a move consumed.  The payload would
//      claim the token of a region that it does not hold.
//   3. An assignment over a DelegatedSession that holds a live endpoint.
//      The assignment drops that endpoint, and its policy acts.
//
// Each attack runs in a child process.  The test passes only when the
// child ends by SIGABRT and its standard error names the rule.

#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <foundation/effects/Row.h>

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {
    int sent = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<int, s::End>;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using SendsRegion = s::Send<s::Transferable<int, Region>, s::End>;

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

void accept_after_a_move() {
    auto parcel = s::mint_delegated_session(s::mint_session_handle<Inner, Wire>(Wire{}));
    auto kept = std::move(parcel);
    auto second = std::move(parcel).accept();
    (void)kept;
    (void)second;
}

void delegate_with_a_consumed_hold() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [handle, hold] = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, fp::mint_permission_root<Region>());
    auto kept = std::move(hold);
    auto parcel = s::mint_delegated_session(std::move(handle), std::move(hold));
    (void)kept;
    (void)parcel;
}

void assign_over_a_live_endpoint() {
    auto target = s::mint_delegated_session(s::mint_session_handle<Inner, Wire>(Wire{}));
    auto source = s::mint_delegated_session(s::mint_session_handle<Inner, Wire>(Wire{}));
    target = std::move(source);
    (void)target;
}

}  // namespace

int main() {
    const bool refuses_second_accept = attack_aborts_with(accept_after_a_move, "ACCEPT OF AN EMPTY DELEGATION");
    const bool refuses_consumed_hold =
        attack_aborts_with(delegate_with_a_consumed_hold, "[Hold_Consumed]: a PermHold was used after a move");
    const bool drops_assigned_over =
        attack_aborts_with(assign_over_a_live_endpoint, "ABANDONMENT DETECTED (non-terminal handle)");
    std::printf("test_session_delegation_aborts: accept after a move %s, consumed hold %s, assignment over a live "
                "endpoint %s\n",
                refuses_second_accept ? "refused" : "NOT REFUSED", refuses_consumed_hold ? "refused" : "NOT REFUSED",
                drops_assigned_over ? "refused" : "NOT REFUSED");
    return refuses_second_accept && refuses_consumed_hold && drops_assigned_over ? 0 : 1;
}
