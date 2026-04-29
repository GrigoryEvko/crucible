// ═══════════════════════════════════════════════════════════════════
// test_inferred_permission_tags — sentinel TU for safety/
//                                  InferredPermissionTags.h
//
// Same blind-spot rationale as test_is_owned_region / test_is_
// permission / test_graded_extract: a header shipped with embedded
// static_asserts is unverified under the project warning flags
// unless a .cpp TU includes it.  This sentinel forces
// InferredPermissionTags.h through the test target's full
// -Werror=shadow / -Werror=conversion / -Wanalyzer-* matrix and
// exercises the runtime_smoke_test inline body.
//
// Coverage extends beyond the header self-test (which only
// exercises tag-free functions) to:
//   * OwnedRegion-only functions — single + multi-tag.
//   * Permission-only functions.
//   * SharedPermission-only functions.
//   * Mixed (OwnedRegion + Permission + SharedPermission) functions.
//   * Deduplication: two parameters sharing a tag → emitted once.
//   * Cv-ref qualified parameters — `OwnedRegion<T, Tag>&&`,
//     `Permission<Tag> const&`, etc.
//   * Canonicalization: declaration-order-independent equality.
//   * Non-tag-bearing parameters are skipped (not guessed).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/InferredPermissionTags.h>

#include <crucible/safety/OwnedRegion.h>
#include <crucible/permissions/Permission.h>

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
namespace safety  = ::crucible::safety;
namespace proto   = ::crucible::safety::proto;

// ── Test region + permission tags ─────────────────────────────────

struct region_tag_a {};
struct region_tag_b {};
struct region_tag_c {};

template <typename T> using OR = safety::OwnedRegion<T, region_tag_a>;
template <typename T> using ORb = safety::OwnedRegion<T, region_tag_b>;
template <typename T> using ORc = safety::OwnedRegion<T, region_tag_c>;
using P_a = safety::Permission<region_tag_a>;
using P_b = safety::Permission<region_tag_b>;
using SP_a = safety::SharedPermission<region_tag_a>;
using SP_b = safety::SharedPermission<region_tag_b>;

// ── Test functions of various shapes ──────────────────────────────

void f_no_tag_params(int, double) noexcept {}
void f_nullary() noexcept {}

void f_one_owned_region(OR<int>&&) noexcept {}
void f_two_owned_regions(OR<int>&&, ORb<float>&&) noexcept {}
void f_three_owned_regions(OR<int>&&, ORb<float>&&, ORc<double>&&) noexcept {}

void f_one_permission(P_a&&) noexcept {}
void f_two_permissions(P_a&&, P_b&&) noexcept {}

void f_shared_permission(SP_a&&) noexcept {}

void f_mixed(OR<int>&&, P_b&&) noexcept {}
void f_mixed_with_int(int, OR<int>&&, double, P_b&&) noexcept {}

// Same tag from BOTH OwnedRegion AND Permission — dedup should fold.
void f_same_tag_two_wrappers(OR<int>&&, P_a&&) noexcept {}

// Two parameters sharing the SAME tag — dedup should fold.
void f_same_tag_two_owned(OR<int>&&, OR<float>&&) noexcept {}

// Cv-ref qualified parameters — must still see through.
void f_cvref_qualified(const OR<int>&, P_a const&, SP_b&&) noexcept {}

// Declaration order: (A, B) vs (B, A) — canonical form must collapse.
void f_order_ab(P_a&&, P_b&&) noexcept {}
void f_order_ba(P_b&&, P_a&&) noexcept {}

void test_runtime_smoke() {
    EXPECT_TRUE(extract::inferred_permission_tags_smoke_test());
}

void test_no_tag_params_yield_empty_set() {
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_no_tag_params>,
        proto::EmptyPermSet>);
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_nullary>,
        proto::EmptyPermSet>);

    static_assert(extract::is_tag_free_function_v<&f_no_tag_params>);
    static_assert(extract::is_tag_free_function_v<&f_nullary>);
}

void test_single_owned_region_yields_single_tag() {
    using TagsExpected = proto::PermSet<region_tag_a>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_one_owned_region>,
        TagsExpected>);

    static_assert(!extract::is_tag_free_function_v<&f_one_owned_region>);
}

void test_two_owned_regions_yield_pair() {
    using TagsExpected = proto::PermSet<region_tag_a, region_tag_b>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_two_owned_regions>,
        TagsExpected>);
}

void test_three_owned_regions_yield_triple() {
    using TagsExpected =
        proto::PermSet<region_tag_a, region_tag_b, region_tag_c>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_three_owned_regions>,
        TagsExpected>);
}

void test_permission_only_yields_tag_set() {
    using TagsExpected = proto::PermSet<region_tag_a>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_one_permission>,
        TagsExpected>);

    using TwoTags = proto::PermSet<region_tag_a, region_tag_b>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_two_permissions>,
        TwoTags>);
}

void test_shared_permission_yields_tag() {
    using TagsExpected = proto::PermSet<region_tag_a>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_shared_permission>,
        TagsExpected>);
}

void test_mixed_owned_region_and_permission() {
    // f_mixed(OR<a>, P_b) → {a, b}.
    using TagsExpected = proto::PermSet<region_tag_a, region_tag_b>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_mixed>,
        TagsExpected>);

    // f_mixed_with_int has int and double interspersed — they must
    // be SKIPPED (no tag contribution), not guessed.
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_mixed_with_int>,
        TagsExpected>);
}

void test_dedup_same_tag_two_wrappers() {
    // f_same_tag_two_wrappers(OR<a>, P_a) → {a}, NOT {a, a}.
    // Different wrappers but same underlying region tag.
    using TagsExpected = proto::PermSet<region_tag_a>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_same_tag_two_wrappers>,
        TagsExpected>);
}

void test_dedup_same_tag_two_owned() {
    // f_same_tag_two_owned(OR<int>&&, OR<float>&&) → {a}, NOT {a, a}.
    // Same wrapper, same tag, different value_type.
    using TagsExpected = proto::PermSet<region_tag_a>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_same_tag_two_owned>,
        TagsExpected>);
}

void test_cvref_qualified_parameters_still_extract() {
    // Cv-ref stripping happens inside is_*_v / *_tag_t, so the
    // harvest sees through the qualifications.  Tags emitted: a, a, b
    // → deduped to {a, b}.
    using TagsExpected = proto::PermSet<region_tag_a, region_tag_b>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_cvref_qualified>,
        TagsExpected>);
}

void test_canonicalization_order_independence() {
    // f_order_ab(P_a, P_b) and f_order_ba(P_b, P_a) project to the
    // SAME canonical PermSet.  Load-bearing for federation cache
    // stability — two functions that take the same tag set in
    // different declaration orders produce IDENTICAL keys.
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&f_order_ab>,
        extract::inferred_permission_tags_t<&f_order_ba>>);
}

void test_concept_form() {
    static_assert(extract::IsTagFreeFunction<&f_no_tag_params>);
    static_assert(extract::IsTagFreeFunction<&f_nullary>);
    static_assert(!extract::IsTagFreeFunction<&f_one_owned_region>);
    static_assert(!extract::IsTagFreeFunction<&f_one_permission>);
    static_assert(!extract::IsTagFreeFunction<&f_mixed>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_empty =
        extract::is_tag_free_function_v<&f_no_tag_params>;
    bool baseline_one =
        !extract::is_tag_free_function_v<&f_one_owned_region>;
    EXPECT_TRUE(baseline_empty);
    EXPECT_TRUE(baseline_one);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_empty
            == extract::is_tag_free_function_v<&f_no_tag_params>);
        EXPECT_TRUE(baseline_one
            == !extract::is_tag_free_function_v<&f_one_owned_region>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_inferred_permission_tags:\n");
    run_test("test_runtime_smoke",
             test_runtime_smoke);
    run_test("test_no_tag_params_yield_empty_set",
             test_no_tag_params_yield_empty_set);
    run_test("test_single_owned_region_yields_single_tag",
             test_single_owned_region_yields_single_tag);
    run_test("test_two_owned_regions_yield_pair",
             test_two_owned_regions_yield_pair);
    run_test("test_three_owned_regions_yield_triple",
             test_three_owned_regions_yield_triple);
    run_test("test_permission_only_yields_tag_set",
             test_permission_only_yields_tag_set);
    run_test("test_shared_permission_yields_tag",
             test_shared_permission_yields_tag);
    run_test("test_mixed_owned_region_and_permission",
             test_mixed_owned_region_and_permission);
    run_test("test_dedup_same_tag_two_wrappers",
             test_dedup_same_tag_two_wrappers);
    run_test("test_dedup_same_tag_two_owned",
             test_dedup_same_tag_two_owned);
    run_test("test_cvref_qualified_parameters_still_extract",
             test_cvref_qualified_parameters_still_extract);
    run_test("test_canonicalization_order_independence",
             test_canonicalization_order_independence);
    run_test("test_concept_form",
             test_concept_form);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
