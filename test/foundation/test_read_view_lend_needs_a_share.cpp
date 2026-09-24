// A share guard that no longer holds its share cannot lend a read view.
//
// A guard moved into another object keeps no pool.  If with_read_view
// took it, the body would read the region under no share, while the
// guard that holds the share can end it.  The door checks that the guard
// holds a share and aborts when it does not.  The attack moves the guard
// out of an optional and then lends the optional's leftover guard, the
// shape a real caller writes by mistake.  It runs in a child process, and
// the test passes only when the child aborts on that check.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace test_read_view_lend_needs_a_share {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

// Runs the attack in a child process.  The test passes only when the
// child ends by SIGABRT and its standard error names the check, so a
// different crash, or no crash, fails it.
[[nodiscard]] static bool attack_aborts_with(void (*attack)(), const char* expected) {
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

static void lend_a_guard_that_gave_its_share_away() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    fp::SharedPermissionPool<Region, Brand> pool{std::move(root)};
    std::optional<fp::SharedPermissionGuard<Region, Brand>> lent = pool.lend();
    fp::SharedPermissionGuard<Region, Brand> holder{std::move(*lent)};
    auto back = fp::with_read_view(std::move(*lent), [](auto const&) noexcept {});
    (void)back;
    (void)holder;
}

// The positive control: the guard that holds the share lends the view,
// and the door hands the guard back.
[[nodiscard]] static bool a_guard_that_holds_its_share_lends() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    fp::SharedPermissionPool<Region, Brand> pool{std::move(root)};
    std::optional<fp::SharedPermissionGuard<Region, Brand>> lent = pool.lend();
    bool read = false;
    auto back = fp::with_read_view(std::move(*lent), [&read](auto const&) noexcept { read = true; });
    return read && back.holds_share();
}

}  // namespace test_read_view_lend_needs_a_share

int main() {
    using namespace test_read_view_lend_needs_a_share;
    const bool lends = a_guard_that_holds_its_share_lends();
    const bool refused = attack_aborts_with(lend_a_guard_that_gave_its_share_away,
                                            "foundation: contract violation: source.holds_share()");
    std::printf("test_read_view_lend_needs_a_share: control %s, attack %s\n", lends ? "lends" : "DOES NOT LEND",
                refused ? "refused" : "NOT REFUSED");
    return lends && refused ? 0 : 1;
}
