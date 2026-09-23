// A pool that ends while a share is out aborts with a named diagnostic.
//
// Each guard keeps a pointer into its pool, and the release of the guard
// writes the pool's count.  Without the check in the pool's destructor,
// the release below writes freed memory.  The attack runs in a child
// process, and the test passes only on that diagnostic.

#include <foundation/permissions/Permission.h>

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

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

void end_pool_while_shared() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using Brand = decltype(root)::brand_type;
    std::optional<fp::SharedPermissionGuard<Region, Brand>> share;
    {
        auto pool = std::make_unique<fp::SharedPermissionPool<Region, Brand>>(std::move(root));
        share.emplace(std::move(*pool->lend()));
    }
}

}  // namespace

int main() {
    const bool refused = attack_aborts_with(end_pool_while_shared, "a SharedPermissionPool ended while shares were out");
    std::printf("test_share_pool_outlived_aborts: %s\n", refused ? "refused" : "NOT REFUSED");
    return refused ? 0 : 1;
}
