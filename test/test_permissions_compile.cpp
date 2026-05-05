// ═══════════════════════════════════════════════════════════════════
// test_permissions_compile — sentinel TU for permissions/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every permissions/* header through the test target's full
// -Werror matrix.  Type-level checks live in this TU or in dedicated
// negative compile fixtures; reaching main proves the include set was
// processed clean.
//
// Coverage: 6 headers (Permission, PermissionFork, PermissionInherit,
// Permissions, PermSet, ReadView).  When a new permissions/* header
// ships, add its include below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/permissions/Permissions.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/ReadView.h>

#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <type_traits>

namespace {

struct TestFailure {};
struct InheritWorkerTag {};
struct InheritCoordTag {};
struct InheritMasterTag {};
struct InheritNonInheritingTag {};

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

}  // namespace

namespace crucible::permissions {

template <>
struct survivor_registry<InheritWorkerTag> {
    using type = inheritance_list<InheritCoordTag>;
};

template <>
struct survivor_registry<InheritCoordTag> {
    using type = inheritance_list<InheritMasterTag>;
};

}  // namespace crucible::permissions

namespace {

void test_permission_compile()      {}
void test_permission_fork_compile() {}
void test_permission_inherit_compile() {
    namespace pi = ::crucible::permissions;
    using WorkerSurvivors = pi::survivors_t<InheritWorkerTag>;
    static_assert(pi::inheritance_list_contains_v<
        WorkerSurvivors, InheritCoordTag>);
    static_assert(!pi::inheritance_list_contains_v<
        WorkerSurvivors, InheritNonInheritingTag>);
    static_assert(pi::inherits_from_v<InheritWorkerTag, InheritCoordTag>);
    static_assert(!pi::inherits_from_v<
        InheritWorkerTag, InheritNonInheritingTag>);

    using ExplicitTuple = decltype(
        pi::permission_inherit<InheritWorkerTag, InheritCoordTag>());
    using RegistryTuple = decltype(pi::permission_inherit<InheritWorkerTag>());
    static_assert(std::is_same_v<
        ExplicitTuple,
        std::tuple<::crucible::safety::Permission<InheritCoordTag>>>);
    static_assert(std::is_same_v<ExplicitTuple, RegistryTuple>);

    using ChainedTuple = decltype(pi::permission_inherit<InheritCoordTag>());
    static_assert(std::is_same_v<
        ChainedTuple,
        std::tuple<::crucible::safety::Permission<InheritMasterTag>>>);
}
void test_permissions_umbrella() {}
void test_perm_set_compile()     {}
void test_read_view_compile()    {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissions_compile:\n");
    run_test("test_permission_compile",      test_permission_compile);
    run_test("test_permission_fork_compile", test_permission_fork_compile);
    run_test("test_permission_inherit_compile", test_permission_inherit_compile);
    run_test("test_permissions_umbrella",    test_permissions_umbrella);
    run_test("test_perm_set_compile",        test_perm_set_compile);
    run_test("test_read_view_compile",       test_read_view_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
