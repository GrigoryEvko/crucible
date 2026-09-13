// A header that ships its own static_asserts stays unverified under the
// project warning flags until some translation unit includes it.  This
// one pulls the permission-trait header through the test target's
// warning matrix and runs its inline smoke body.

#include <crucible/safety/IsPermission.h>

#include <crucible/safety/Linear.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Tagged.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace extract = ::crucible::safety::extract;

struct test_tag_x {};
struct test_tag_y {};

using P_x = ::crucible::safety::Permission<test_tag_x>;
using P_y = ::crucible::safety::Permission<test_tag_y>;
using SP_x = ::crucible::safety::SharedPermission<test_tag_x>;
using SP_y = ::crucible::safety::SharedPermission<test_tag_y>;
using SG_x = ::crucible::safety::SharedPermissionGuard<test_tag_x>;

void test_runtime_smoke() { EXPECT_TRUE(extract::is_permission_smoke_test()); }

void test_positive_permission() {
    static_assert(extract::is_permission_v<P_x>);
    static_assert(extract::is_permission_v<P_y>);
}

void test_positive_shared_permission() {
    static_assert(extract::is_shared_permission_v<SP_x>);
    static_assert(extract::is_shared_permission_v<SP_y>);
}

void test_cvref_stripping_permission() {
    static_assert(extract::is_permission_v<P_x&>);
    static_assert(extract::is_permission_v<P_x&&>);
    static_assert(extract::is_permission_v<P_x const>);
    static_assert(extract::is_permission_v<P_x const&>);
    static_assert(extract::is_permission_v<P_x const&&>);
    static_assert(extract::is_permission_v<P_x volatile>);
    static_assert(extract::is_permission_v<P_x const volatile>);
}

void test_cvref_stripping_shared_permission() {
    static_assert(extract::is_shared_permission_v<SP_x&>);
    static_assert(extract::is_shared_permission_v<SP_x&&>);
    static_assert(extract::is_shared_permission_v<SP_x const>);
    static_assert(extract::is_shared_permission_v<SP_x const&>);
    static_assert(extract::is_shared_permission_v<SP_x volatile>);
    static_assert(extract::is_shared_permission_v<SP_x const volatile>);
}

void test_negative_permission() {
    static_assert(!extract::is_permission_v<int>);
    static_assert(!extract::is_permission_v<int*>);
    static_assert(!extract::is_permission_v<int&>);
    static_assert(!extract::is_permission_v<int&&>);
    static_assert(!extract::is_permission_v<void>);
    static_assert(!extract::is_permission_v<test_tag_x>);
    // The linear and the fractional permission must not alias.
    static_assert(!extract::is_permission_v<SP_x>);
    static_assert(!extract::is_permission_v<SG_x>);
}

void test_negative_shared_permission() {
    static_assert(!extract::is_shared_permission_v<int>);
    static_assert(!extract::is_shared_permission_v<int*>);
    static_assert(!extract::is_shared_permission_v<void>);
    static_assert(!extract::is_shared_permission_v<test_tag_x>);
    static_assert(!extract::is_shared_permission_v<P_x>);
    static_assert(!extract::is_shared_permission_v<SG_x>);
}

void test_pointer_to_permission_rejected() {
    // remove_cvref does not strip pointers, so a pointer to a
    // Permission is not itself one.  A refactor that added pointer
    // decay to the trait would break here.
    using PtrP = P_x*;
    static_assert(!extract::is_permission_v<PtrP>);
    static_assert(!extract::is_permission_v<P_x* const>);
    static_assert(!extract::is_permission_v<P_x const*>);
    static_assert(!extract::is_permission_v<P_x* const&>);
    static_assert(!extract::is_shared_permission_v<SP_x*>);
    static_assert(!extract::is_shared_permission_v<SP_x const*>);
}

void test_lookalike_rejected() {
    // The trait must key on template identity, not structural shape.
    // An empty struct of the same size is not a Permission.
    struct LookalikePermission {};
    static_assert(!extract::is_permission_v<LookalikePermission>);
    static_assert(!extract::is_shared_permission_v<LookalikePermission>);
}

void test_concept_form() {
    static_assert(extract::IsPermission<P_x>);
    static_assert(extract::IsPermission<P_x&&>);
    static_assert(extract::IsPermission<P_x const&>);
    static_assert(!extract::IsPermission<int>);
    static_assert(!extract::IsPermission<test_tag_x>);
    static_assert(!extract::IsPermission<SP_x>);

    static_assert(extract::IsSharedPermission<SP_x>);
    static_assert(extract::IsSharedPermission<SP_x&&>);
    static_assert(extract::IsSharedPermission<SP_x const&>);
    static_assert(!extract::IsSharedPermission<int>);
    static_assert(!extract::IsSharedPermission<P_x>);
}

void test_permission_tag_extraction() {
    static_assert(std::is_same_v<extract::permission_tag_t<P_x>, test_tag_x>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_y>, test_tag_y>);
}

void test_shared_permission_tag_extraction() {
    static_assert(std::is_same_v<extract::shared_permission_tag_t<SP_x>, test_tag_x>);
    static_assert(std::is_same_v<extract::shared_permission_tag_t<SP_y>, test_tag_y>);
}

void test_extraction_cvref_stripped() {
    static_assert(std::is_same_v<extract::permission_tag_t<P_x&>, test_tag_x>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_x const&>, test_tag_x>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_x&&>, test_tag_x>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_x const&&>, test_tag_x>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_x volatile>, test_tag_x>);

    static_assert(std::is_same_v<extract::shared_permission_tag_t<SP_x&>, test_tag_x>);
    static_assert(std::is_same_v<extract::shared_permission_tag_t<SP_x const&>, test_tag_x>);
    static_assert(std::is_same_v<extract::shared_permission_tag_t<SP_x&&>, test_tag_x>);
}

void test_distinct_tags_are_distinguished() {
    // Both permissions are empty classes of the same size, so the tag
    // is the only thing that tells them apart.
    static_assert(!std::is_same_v<extract::permission_tag_t<P_x>, extract::permission_tag_t<P_y>>);

    static_assert(!std::is_same_v<extract::shared_permission_tag_t<SP_x>, extract::shared_permission_tag_t<SP_y>>);
}

void test_cross_wrapper_tag_agreement() {
    // The two wrappers differ but name one region, so tag extraction
    // must see through the wrapper shape and agree.
    static_assert(std::is_same_v<extract::permission_tag_t<P_x>, extract::shared_permission_tag_t<SP_x>>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_y>, extract::shared_permission_tag_t<SP_y>>);
}

void test_primitive_tag_rejected() {
    // Permission<Tag> gates its own instantiation on PermissionTag, so
    // a bad tag cannot be instantiated here at all.  What is checkable
    // in-process is the gating concept itself.  If it drifts, the
    // negative-compile fixtures that rely on it stop catching anything,
    // which makes this the upstream check.
    static_assert(!::crucible::safety::PermissionTag<int>);
    static_assert(!::crucible::safety::PermissionTag<int*>);
    static_assert(!::crucible::safety::PermissionTag<int&>);
    static_assert(!::crucible::safety::PermissionTag<void>);
    enum test_enum_tag : int {
        test_enum_value = 0
    };
    enum class test_enum_class_tag {
        value = 0
    };
    union test_union_tag {
        int a;
        double b;
    };
    struct test_stateful_tag {
        int payload = 0;
    };
    static_assert(!::crucible::safety::PermissionTag<test_enum_tag>);
    static_assert(!::crucible::safety::PermissionTag<test_enum_class_tag>);
    static_assert(!::crucible::safety::PermissionTag<test_union_tag>);
    static_assert(!::crucible::safety::PermissionTag<test_stateful_tag>);
    static_assert(::crucible::safety::PermissionTag<test_tag_x>);
    static_assert(::crucible::safety::PermissionTag<test_tag_y>);
}

void test_nested_wrapper_rejection() {
    // A wrapper holding a Permission is not a Permission.  The tag
    // harvest counts direct parameters only, so the trait must not
    // unwrap one level into the held value.
    using L_P = ::crucible::safety::Linear<P_x>;
    static_assert(!extract::is_permission_v<L_P>);
    static_assert(!extract::is_shared_permission_v<L_P>);

    struct ProvenanceTag {};
    using T_P = ::crucible::safety::Tagged<P_x, ProvenanceTag>;
    static_assert(!extract::is_permission_v<T_P>);

    struct RegionTag {};
    using OR_P = ::crucible::safety::OwnedRegion<P_x, RegionTag>;
    static_assert(!extract::is_permission_v<OR_P>);
}

void test_array_and_function_type_rejection() {
    // remove_cvref_t strips neither arrays nor function types.  A
    // refactor that added array or function decay would break here.
    using P_arr5 = P_x[5];
    static_assert(!extract::is_permission_v<P_arr5>);
    static_assert(!extract::is_shared_permission_v<P_arr5>);

    using SP_arr3 = SP_x[3];
    static_assert(!extract::is_shared_permission_v<SP_arr3>);

    using P_fn = P_x(int);
    static_assert(!extract::is_permission_v<P_fn>);

    using P_memptr = P_x test_tag_x::*;
    static_assert(!extract::is_permission_v<P_memptr>);
}

void test_is_permission_for_concept() {
    static_assert(extract::IsPermissionFor<P_x, test_tag_x>);
    static_assert(extract::IsPermissionFor<P_x&&, test_tag_x>);
    static_assert(extract::IsPermissionFor<const P_x&, test_tag_x>);
    static_assert(!extract::IsPermissionFor<P_x, test_tag_y>);
    static_assert(!extract::IsPermissionFor<P_y, test_tag_x>);
    static_assert(!extract::IsPermissionFor<int, test_tag_x>);
    static_assert(!extract::IsPermissionFor<SP_x, test_tag_x>);

    static_assert(extract::IsSharedPermissionFor<SP_x, test_tag_x>);
    static_assert(!extract::IsSharedPermissionFor<SP_x, test_tag_y>);
    static_assert(!extract::IsSharedPermissionFor<P_x, test_tag_x>);
}

void test_runtime_consistency() {
    // The volatile bound keeps the loop out of reach of constant
    // folding, so each iteration re-reads the predicate.
    volatile std::size_t const cap = 50;
    bool baseline_p = extract::is_permission_v<P_x>;
    bool baseline_sp = extract::is_shared_permission_v<SP_x>;
    EXPECT_TRUE(baseline_p);
    EXPECT_TRUE(baseline_sp);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_p == extract::is_permission_v<P_x>);
        EXPECT_TRUE(baseline_sp == extract::is_shared_permission_v<SP_x>);
        EXPECT_TRUE(!extract::is_permission_v<int>);
        EXPECT_TRUE(!extract::is_shared_permission_v<int>);
        EXPECT_TRUE(!extract::is_permission_v<SP_x>);
        EXPECT_TRUE(!extract::is_shared_permission_v<P_x>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_permission:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_permission", test_positive_permission);
    run_test("test_positive_shared_permission", test_positive_shared_permission);
    run_test("test_cvref_stripping_permission", test_cvref_stripping_permission);
    run_test("test_cvref_stripping_shared_permission", test_cvref_stripping_shared_permission);
    run_test("test_negative_permission", test_negative_permission);
    run_test("test_negative_shared_permission", test_negative_shared_permission);
    run_test("test_pointer_to_permission_rejected", test_pointer_to_permission_rejected);
    run_test("test_lookalike_rejected", test_lookalike_rejected);
    run_test("test_concept_form", test_concept_form);
    run_test("test_permission_tag_extraction", test_permission_tag_extraction);
    run_test("test_shared_permission_tag_extraction", test_shared_permission_tag_extraction);
    run_test("test_extraction_cvref_stripped", test_extraction_cvref_stripped);
    run_test("test_distinct_tags_are_distinguished", test_distinct_tags_are_distinguished);
    run_test("test_cross_wrapper_tag_agreement", test_cross_wrapper_tag_agreement);
    run_test("test_primitive_tag_rejected", test_primitive_tag_rejected);
    run_test("test_nested_wrapper_rejection", test_nested_wrapper_rejection);
    run_test("test_array_and_function_type_rejection", test_array_and_function_type_rejection);
    run_test("test_is_permission_for_concept", test_is_permission_for_concept);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
