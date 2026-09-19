// The pool exists to keep shared access and exclusive access mutually
// exclusive. The attack on that invariant is to copy a shared token out of
// a guard, stash it past the guard's scope, and then upgrade: an exclusive
// permission is handed out while a shared token is still live.
//
// The resolution is that a shared token confers no runtime access at all.
// It is a type-level witness. The invariant is enforced over guards, which
// are the access carriers: a guard is live exactly while the count is
// above zero, and the upgrade fails exactly then. These tests pin that.

#include <foundation/permissions/Permission.h>

#include <cstdio>
#include <optional>
#include <utility>

using namespace foundation::permissions;

namespace {

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

struct Region {};

static_assert(SharedPermission<Region>::confers_runtime_access == false,
              "SharedPermission must confer no runtime access");

// The attack run verbatim. Because the access carrier is the guard and not
// the token, the upgrade is blocked exactly while a guard is live, however
// many tokens were copied out and stashed.
void test_stashed_token_does_not_unblock_upgrade() {
    auto exc = mint_permission_root<Region>();
    SharedPermissionPool<Region> pool{std::move(exc)};

    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    std::optional<SharedPermission<Region>> stashed_token;
    {
        auto guard = pool.lend();
        CRUCIBLE_TEST_REQUIRE(guard.has_value());
        CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);

        // The attack step: copy the token out, past the guard's scope.
        stashed_token = guard->token();

        auto blocked = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(!blocked.has_value());
        CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    }
    // The guard is gone and the count is back to zero. The stashed token is
    // still alive, but it carries no access, so no shared reader survives
    // and the upgrade below is sound.
    CRUCIBLE_TEST_REQUIRE(stashed_token.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    auto upgraded = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(upgraded.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.is_exclusive_out());

    // The other half of the invariant: while the exclusive permission is
    // out, lending must fail until it is deposited back.
    auto cannot_lend = pool.lend();
    CRUCIBLE_TEST_REQUIRE(!cannot_lend.has_value());

    pool.deposit_exclusive(std::move(*upgraded));
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    auto relent = pool.lend();
    CRUCIBLE_TEST_REQUIRE(relent.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
}

void test_multiple_guards_block_upgrade() {
    auto exc = mint_permission_root<Region>();
    SharedPermissionPool<Region> pool{std::move(exc)};

    auto g1 = pool.lend();
    auto g2 = pool.lend();
    CRUCIBLE_TEST_REQUIRE(g1.has_value() && g2.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 2);

    // A token from each guard, to show that copying one out does not move
    // the carrier count.
    [[maybe_unused]] auto t1 = g1->token();
    [[maybe_unused]] auto t2 = g2->token();

    CRUCIBLE_TEST_REQUIRE(!pool.try_upgrade().has_value());

    g1.reset();
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
    CRUCIBLE_TEST_REQUIRE(!pool.try_upgrade().has_value());

    g2.reset();
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    auto up = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(up.has_value());
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permission_shared_xor_exclusive:\n");
    run_test("stashed_token_does_not_unblock_upgrade", test_stashed_token_does_not_unblock_upgrade);
    run_test("multiple_guards_block_upgrade", test_multiple_guards_block_upgrade);

    std::fprintf(stderr, "  passed=%d failed=%d\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
