// ═══════════════════════════════════════════════════════════════════
// test_permission_shared_xor_exclusive — fix-06 regression
//
// Pins the shared-XOR-exclusive invariant that SharedPermissionPool
// exists to enforce, and the fix-06 resolution of the "stashed token
// outlives its Guard" exploit.
//
// The exploit framing was: lend() → Guard (count = 1) → copy
// guard.token() and stash it → destroy the Guard (count → 0) →
// try_upgrade() succeeds and hands out the EXCLUSIVE Permission while a
// copied SharedPermission token is still live, "defeating shared-XOR-
// exclusive".
//
// fix-06 resolution (option c, made explicit):
//   * SharedPermission CONFERS NO RUNTIME ACCESS
//     (confers_runtime_access == false) — a stashed token is a pure
//     type-level witness, not a live read-proof.
//   * The shared-XOR-exclusive invariant is enforced over GUARDS (the
//     access carriers): a Guard live ⟺ count > 0 ⟺ try_upgrade fails.
//
// These tests prove the Guard-refcount invariant directly: try_upgrade
// is BLOCKED while any Guard is live (even if a token copy is stashed),
// and SUCCEEDS only once every Guard is gone.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <optional>
#include <utility>

using namespace crucible::safety;

namespace {

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
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

// fix-06 structural marker: the token confers no runtime access.
static_assert(SharedPermission<Region>::confers_runtime_access == false,
              "fix-06: SharedPermission must confer no runtime access");

// ── The exploit sequence, neutralized ────────────────────────────────
//
// Reproduce the exploit verbatim and show that, because the access
// carrier is the Guard (not the token), try_upgrade is blocked exactly
// while a Guard is live — independent of how many tokens were copied
// out and stashed.
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

        // Step of the exploit: copy the token out and stash it past the
        // Guard's scope.
        stashed_token = guard->token();

        // While the Guard is live, try_upgrade MUST fail (count > 0).
        auto blocked = pool.try_upgrade();
        CRUCIBLE_TEST_REQUIRE(!blocked.has_value());
        CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());
    }
    // Guard destroyed → count → 0.  The stashed token is STILL alive,
    // but it confers no access, so the shared-XOR-exclusive invariant
    // over the access carriers is intact: no Guard is live now.
    CRUCIBLE_TEST_REQUIRE(stashed_token.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 0);

    // try_upgrade now succeeds — and that is SOUND precisely because the
    // stashed token grants nothing.  No live shared READ (= Guard) and
    // the exclusive Permission coexist for region Region.
    auto upgraded = pool.try_upgrade();
    CRUCIBLE_TEST_REQUIRE(upgraded.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.is_exclusive_out());

    // The exclusive is now out; further lend() must fail until it is
    // deposited back — confirming the other half of the XOR.
    auto cannot_lend = pool.lend();
    CRUCIBLE_TEST_REQUIRE(!cannot_lend.has_value());

    pool.deposit_exclusive(std::move(*upgraded));
    CRUCIBLE_TEST_REQUIRE(!pool.is_exclusive_out());

    // After deposit, lending works again.
    auto relent = pool.lend();
    CRUCIBLE_TEST_REQUIRE(relent.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 1);
}

// ── Multiple live Guards all block upgrade ────────────────────────────
void test_multiple_guards_block_upgrade() {
    auto exc = mint_permission_root<Region>();
    SharedPermissionPool<Region> pool{std::move(exc)};

    auto g1 = pool.lend();
    auto g2 = pool.lend();
    CRUCIBLE_TEST_REQUIRE(g1.has_value() && g2.has_value());
    CRUCIBLE_TEST_REQUIRE(pool.outstanding() == 2);

    // Stash a token from each — still no effect on the carrier count.
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
    run_test("stashed_token_does_not_unblock_upgrade",
             test_stashed_token_does_not_unblock_upgrade);
    run_test("multiple_guards_block_upgrade",
             test_multiple_guards_block_upgrade);

    std::fprintf(stderr, "  passed=%d failed=%d\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
