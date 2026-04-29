// ═══════════════════════════════════════════════════════════════════
// test_is_permission — sentinel TU for safety/IsPermission.h
//
// Same blind-spot rationale as test_is_owned_region / test_signature_
// traits / test_stable_name_compile (see feedback_header_only_static_
// assert_blind_spot memory): a header shipped with embedded static_
// asserts is unverified under the project warning flags unless a .cpp
// TU includes it.  This sentinel forces IsPermission.h through the
// test target's full -Werror=shadow / -Werror=conversion / -Wanalyzer-*
// matrix and exercises the runtime_smoke_test inline body.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution.
//   * Positive: Permission<Tag> / SharedPermission<Tag> + every cv-ref
//     qualified form.
//   * Negative: int, int*, int&, void, foreign struct, the OPPOSITE
//     wrapper (Permission ↛ shared, shared ↛ linear), the RAII guard.
//   * IsPermission / IsSharedPermission concept forms.
//   * Tag extraction with cv-ref stripping; round-trip identity;
//     distinct-tag distinction; cross-wrapper tag agreement.
//   * Pointer-to-Permission rejection (remove_cvref does NOT strip).
// ═══════════════════════════════════════════════════════════════════

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;

struct test_tag_x {};
struct test_tag_y {};

using P_x  = ::crucible::safety::Permission<test_tag_x>;
using P_y  = ::crucible::safety::Permission<test_tag_y>;
using SP_x = ::crucible::safety::SharedPermission<test_tag_x>;
using SP_y = ::crucible::safety::SharedPermission<test_tag_y>;
using SG_x = ::crucible::safety::SharedPermissionGuard<test_tag_x>;

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_permission_smoke_test());
}

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
    // The OPPOSITE wrapper — fractional vs linear must not alias.
    static_assert(!extract::is_permission_v<SP_x>);
    static_assert(!extract::is_permission_v<SG_x>);
}

void test_negative_shared_permission() {
    static_assert(!extract::is_shared_permission_v<int>);
    static_assert(!extract::is_shared_permission_v<int*>);
    static_assert(!extract::is_shared_permission_v<void>);
    static_assert(!extract::is_shared_permission_v<test_tag_x>);
    // The OPPOSITE wrapper — linear vs fractional must not alias.
    static_assert(!extract::is_shared_permission_v<P_x>);
    static_assert(!extract::is_shared_permission_v<SG_x>);
}

void test_pointer_to_permission_rejected() {
    // remove_cvref does NOT strip pointers — a pointer-to-Permission
    // is NOT itself a Permission.  Catches the bug-class where a
    // refactor adds incorrect pointer-decay to the trait.
    using PtrP = P_x*;
    static_assert(!extract::is_permission_v<PtrP>);
    static_assert(!extract::is_permission_v<P_x* const>);
    static_assert(!extract::is_permission_v<P_x const*>);
    static_assert(!extract::is_permission_v<P_x* const&>);
    static_assert(!extract::is_shared_permission_v<SP_x*>);
    static_assert(!extract::is_shared_permission_v<SP_x const*>);
}

void test_lookalike_rejected() {
    // A struct that walks like a Permission (sizeof == 1, empty)
    // but is not the Permission template specialization is rejected.
    // Catches the bug-class where the trait is loosened to detect
    // structural shape rather than template identity.
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
    static_assert(!extract::IsPermission<SP_x>);  // crucial discrimination

    static_assert(extract::IsSharedPermission<SP_x>);
    static_assert(extract::IsSharedPermission<SP_x&&>);
    static_assert(extract::IsSharedPermission<SP_x const&>);
    static_assert(!extract::IsSharedPermission<int>);
    static_assert(!extract::IsSharedPermission<P_x>);  // crucial
}

void test_permission_tag_extraction() {
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_y>, test_tag_y>);
}

void test_shared_permission_tag_extraction() {
    static_assert(std::is_same_v<
        extract::shared_permission_tag_t<SP_x>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::shared_permission_tag_t<SP_y>, test_tag_y>);
}

void test_extraction_cvref_stripped() {
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x const&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x&&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x const&&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x volatile>, test_tag_x>);

    static_assert(std::is_same_v<
        extract::shared_permission_tag_t<SP_x&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::shared_permission_tag_t<SP_x const&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::shared_permission_tag_t<SP_x&&>, test_tag_x>);
}

void test_distinct_tags_are_distinguished() {
    // Two Permission<different-tag> are always TypeSafe-distinguishable
    // even though both have sizeof == 1 (both empty classes).
    static_assert(!std::is_same_v<
        extract::permission_tag_t<P_x>,
        extract::permission_tag_t<P_y>>);

    static_assert(!std::is_same_v<
        extract::shared_permission_tag_t<SP_x>,
        extract::shared_permission_tag_t<SP_y>>);
}

void test_cross_wrapper_tag_agreement() {
    // Permission<X> and SharedPermission<X> share the SAME Tag —
    // the wrapper differs (linear vs fractional) but the underlying
    // region tag is identical.  Validates that the tag-extraction
    // surface sees through the wrapper-shape distinction.
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_x>,
        extract::shared_permission_tag_t<SP_x>>);
    static_assert(std::is_same_v<
        extract::permission_tag_t<P_y>,
        extract::shared_permission_tag_t<SP_y>>);
}

void test_primitive_tag_positive() {
    // Tag is open over the partial spec — non-class tag types resolve.
    using P_int = ::crucible::safety::Permission<int>;
    static_assert(extract::is_permission_v<P_int>);
    static_assert(std::is_same_v<extract::permission_tag_t<P_int>, int>);
}

void test_nested_wrapper_rejection() {
    // Linear<Permission<X>> is a Linear holding a Permission, NOT a
    // Permission.  The dispatcher's tag-harvest (FOUND-D11) MUST NOT
    // recurse into wrapped Permissions — only DIRECT-parameter
    // Permissions count.  This case proves the trait does not falsely
    // unwrap one level into the held value.
    using L_P = ::crucible::safety::Linear<P_x>;
    static_assert(!extract::is_permission_v<L_P>);
    static_assert(!extract::is_shared_permission_v<L_P>);

    // Tagged<Permission<X>, S> — same shape, different wrapper.
    struct ProvenanceTag {};
    using T_P = ::crucible::safety::Tagged<P_x, ProvenanceTag>;
    static_assert(!extract::is_permission_v<T_P>);

    // OwnedRegion<Permission<X>, R> — region whose ELEMENT TYPE is
    // Permission.  Distinct from Permission<R>.
    struct RegionTag {};
    using OR_P = ::crucible::safety::OwnedRegion<P_x, RegionTag>;
    static_assert(!extract::is_permission_v<OR_P>);
}

void test_array_and_function_type_rejection() {
    // remove_cvref_t does NOT strip arrays or function types.  An
    // array of Permission is not itself a Permission; a function
    // returning Permission is not itself a Permission.  Catches the
    // bug-class where a refactor adds incorrect array/function decay.
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
    // Tag-bound concepts — verify the dispatcher's per-parameter
    // "is this a Permission for THIS specific tag?" check.
    static_assert(extract::IsPermissionFor<P_x, test_tag_x>);
    static_assert(extract::IsPermissionFor<P_x&&, test_tag_x>);
    static_assert(extract::IsPermissionFor<const P_x&, test_tag_x>);
    static_assert(!extract::IsPermissionFor<P_x, test_tag_y>);
    static_assert(!extract::IsPermissionFor<P_y, test_tag_x>);
    static_assert(!extract::IsPermissionFor<int, test_tag_x>);
    // Crucially, IsPermissionFor must NOT admit a SharedPermission
    // even when the tag matches — the linear-vs-fractional discrim
    // is preserved at the binding level.
    static_assert(!extract::IsPermissionFor<SP_x, test_tag_x>);

    static_assert(extract::IsSharedPermissionFor<SP_x, test_tag_x>);
    static_assert(!extract::IsSharedPermissionFor<SP_x, test_tag_y>);
    static_assert(!extract::IsSharedPermissionFor<P_x, test_tag_x>);
}

void test_runtime_consistency() {
    // Volatile-bounded loop confirms the predicates are bit-stable.
    volatile std::size_t const cap = 50;
    bool baseline_p  = extract::is_permission_v<P_x>;
    bool baseline_sp = extract::is_shared_permission_v<SP_x>;
    EXPECT_TRUE(baseline_p);
    EXPECT_TRUE(baseline_sp);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_p == extract::is_permission_v<P_x>);
        EXPECT_TRUE(baseline_sp == extract::is_shared_permission_v<SP_x>);
        EXPECT_TRUE(!extract::is_permission_v<int>);
        EXPECT_TRUE(!extract::is_shared_permission_v<int>);
        // Linear and fractional must NEVER cross at the trait level.
        EXPECT_TRUE(!extract::is_permission_v<SP_x>);
        EXPECT_TRUE(!extract::is_shared_permission_v<P_x>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_permission:\n");
    run_test("test_runtime_smoke",                  test_runtime_smoke);
    run_test("test_positive_permission",            test_positive_permission);
    run_test("test_positive_shared_permission",     test_positive_shared_permission);
    run_test("test_cvref_stripping_permission",     test_cvref_stripping_permission);
    run_test("test_cvref_stripping_shared_permission",
                                                    test_cvref_stripping_shared_permission);
    run_test("test_negative_permission",            test_negative_permission);
    run_test("test_negative_shared_permission",     test_negative_shared_permission);
    run_test("test_pointer_to_permission_rejected", test_pointer_to_permission_rejected);
    run_test("test_lookalike_rejected",             test_lookalike_rejected);
    run_test("test_concept_form",                   test_concept_form);
    run_test("test_permission_tag_extraction",      test_permission_tag_extraction);
    run_test("test_shared_permission_tag_extraction",
                                                    test_shared_permission_tag_extraction);
    run_test("test_extraction_cvref_stripped",      test_extraction_cvref_stripped);
    run_test("test_distinct_tags_are_distinguished",
                                                    test_distinct_tags_are_distinguished);
    run_test("test_cross_wrapper_tag_agreement",    test_cross_wrapper_tag_agreement);
    run_test("test_primitive_tag_positive",         test_primitive_tag_positive);
    run_test("test_nested_wrapper_rejection",       test_nested_wrapper_rejection);
    run_test("test_array_and_function_type_rejection",
                                                    test_array_and_function_type_rejection);
    run_test("test_is_permission_for_concept",      test_is_permission_for_concept);
    run_test("test_runtime_consistency",            test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
