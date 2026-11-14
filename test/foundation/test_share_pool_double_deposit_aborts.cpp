// A pool that still parks its exclusive refuses a second deposit, and a
// pool that lends a share refuses a deposit.
//
// A brand names a mint site, so two root mints through one site are two
// tokens of one type, and either fits the pool.  Without the check of the
// state in deposit_exclusive, the second deposit overwrites the parked
// token, and the one region has had two owners.  A deposit into a pool
// that lends stores zero over the live count, and the next upgrade takes
// the region from under a reader.  Each attack runs in a child process,
// and the test passes only when the child ends by SIGABRT with the failed
// condition in its diagnostic.
//
// Two targets build this file.  test_share_pool_deposit_under_ignore
// builds it with the ignore semantic, where a precondition is only an
// assumption.  So the check of the state must be a fatal invariant.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

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

// One mint site, so every call returns one type.
[[nodiscard]] auto mint_region() noexcept { return ::foundation::permissions::mint_permission_root<Region>(); }

// Runs the attack in a child process.  The test passes only when the
// child ends by SIGABRT and its standard error names the condition, so a
// different crash, or no crash, fails it.
[[nodiscard]] bool attack_aborts_with(void (*attack)(), const char* expected) {
    int channel[2];
    if (::pipe(channel) != 0) return false;
    // The child runs the attack, so that its abort is the result the test reads.
    const ::pid_t child = ::fork();
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
    ::waitpid(child, &status, 0);
    const bool aborted = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
    const bool named = text.find(expected) != std::string::npos;
    if (!aborted || !named) std::fprintf(stderr, "child output:\n%s\n", text.c_str());
    return aborted && named;
}

void deposit_while_parked() {
    namespace fp = ::foundation::permissions;
    auto first = mint_region();
    auto second = mint_region();
    fp::SharedPermissionPool pool{std::move(first)};
    pool.deposit_exclusive(std::move(second));
}

// The body runs under one lent share of the pool, and deposits there.
void deposit_while_lending() {
    namespace fp = ::foundation::permissions;
    auto first = mint_region();
    auto second = mint_region();
    fp::SharedPermissionPool pool{std::move(first)};
    fp::with_shared_read(
        pool, [&pool, token = std::move(second)](auto) mutable noexcept { pool.deposit_exclusive(std::move(token)); });
}

}  // namespace

int main() {
    const bool is_parked_refused =
        attack_aborts_with(deposit_while_parked, "fatal invariant failed: is_exclusive_out()");
    const bool is_lending_refused =
        attack_aborts_with(deposit_while_lending, "fatal invariant failed: is_exclusive_out()");
    const bool refused = is_parked_refused && is_lending_refused;
    std::printf("test_share_pool_double_deposit_aborts: %s\n", refused ? "refused" : "NOT REFUSED");
    return refused ? 0 : 1;
}
