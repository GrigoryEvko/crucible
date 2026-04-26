// ═══════════════════════════════════════════════════════════════════
// test_permissions_compile — sentinel TU for permissions/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every permissions/* header through the test target's full
// -Werror matrix; embedded static_assert blocks fire at TU-include
// time, so reaching the run_test invocation proves the include was
// processed clean.
//
// Coverage: 4 headers (Permission, PermissionFork, Permissions,
// ReadView).  When a new permissions/* header ships, add its include
// below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/Permissions.h>
#include <crucible/permissions/ReadView.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct TestFailure {};
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

void test_permission_compile()       {}
void test_permission_fork_compile()  {}
void test_permissions_umbrella()     {}
void test_read_view_compile()        {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissions_compile:\n");
    run_test("test_permission_compile",      test_permission_compile);
    run_test("test_permission_fork_compile", test_permission_fork_compile);
    run_test("test_permissions_umbrella",    test_permissions_umbrella);
    run_test("test_read_view_compile",       test_read_view_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
