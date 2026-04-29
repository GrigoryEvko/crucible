// ═══════════════════════════════════════════════════════════════════
// test_is_owned_region — sentinel TU for safety/IsOwnedRegion.h
//
// Same blind-spot rationale as test_signature_traits / test_stable_
// name_compile (see feedback_header_only_static_assert_blind_spot
// memory): a header shipped with embedded static_asserts is
// unverified under the project warning flags unless a .cpp TU
// includes it.  This sentinel forces IsOwnedRegion.h through the test
// target's full -Werror=shadow / -Werror=conversion / -Wanalyzer-*
// matrix and exercises the runtime_smoke_test inline body.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * runtime_smoke_test() execution.
//   * Positive: OwnedRegion<T, Tag> and every cv-ref qualified form.
//   * Negative: int, int*, int&, void, foreign struct.
//   * IsOwnedRegion concept form.
//   * Element-type and tag extraction with cv-ref stripping.
//   * Distinct (T, Tag) parity / non-parity.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsOwnedRegion.h>

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

using OR_int_x   = ::crucible::safety::OwnedRegion<int, test_tag_x>;
using OR_float_x = ::crucible::safety::OwnedRegion<float, test_tag_x>;
using OR_int_y   = ::crucible::safety::OwnedRegion<int, test_tag_y>;

void test_runtime_smoke() {
    EXPECT_TRUE(extract::runtime_smoke_test());
}

void test_positive_cases() {
    static_assert(extract::is_owned_region_v<OR_int_x>);
    static_assert(extract::is_owned_region_v<OR_float_x>);
    static_assert(extract::is_owned_region_v<OR_int_y>);
}

void test_cvref_stripping() {
    static_assert(extract::is_owned_region_v<OR_int_x&>);
    static_assert(extract::is_owned_region_v<OR_int_x&&>);
    static_assert(extract::is_owned_region_v<OR_int_x const>);
    static_assert(extract::is_owned_region_v<OR_int_x const&>);
    static_assert(extract::is_owned_region_v<OR_int_x const&&>);
    static_assert(extract::is_owned_region_v<OR_int_x volatile>);
    static_assert(extract::is_owned_region_v<OR_int_x const volatile>);
}

void test_negative_cases() {
    static_assert(!extract::is_owned_region_v<int>);
    static_assert(!extract::is_owned_region_v<int*>);
    static_assert(!extract::is_owned_region_v<int&>);
    static_assert(!extract::is_owned_region_v<int&&>);
    static_assert(!extract::is_owned_region_v<void>);
    static_assert(!extract::is_owned_region_v<test_tag_x>);
}

void test_lookalike_rejected() {
    // A struct that walks like an OwnedRegion (base + count) but is
    // not the OwnedRegion template specialization is rejected.
    struct Lookalike { int* base; std::size_t count; };
    static_assert(!extract::is_owned_region_v<Lookalike>);
}

void test_concept_form() {
    static_assert(extract::IsOwnedRegion<OR_int_x>);
    static_assert(extract::IsOwnedRegion<OR_int_x&&>);
    static_assert(extract::IsOwnedRegion<OR_int_x const&>);
    static_assert(!extract::IsOwnedRegion<int>);
    static_assert(!extract::IsOwnedRegion<test_tag_x>);
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x>, int>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_float_x>, float>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_y>, int>);
}

void test_tag_extraction() {
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_x>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_float_x>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_y>, test_tag_y>);
}

void test_extraction_cvref_stripped() {
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x&>, int>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x const&>, int>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x&&>, int>);

    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_x&>, test_tag_x>);
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_x const&>, test_tag_x>);
}

void test_distinct_specializations() {
    // Same value_type, different tag → tag distinguishes.
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x>,
        extract::owned_region_value_t<OR_int_y>>);
    static_assert(!std::is_same_v<
        extract::owned_region_tag_t<OR_int_x>,
        extract::owned_region_tag_t<OR_int_y>>);

    // Same tag, different value_type → value_type distinguishes.
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_x>,
        extract::owned_region_tag_t<OR_float_x>>);
    static_assert(!std::is_same_v<
        extract::owned_region_value_t<OR_int_x>,
        extract::owned_region_value_t<OR_float_x>>);
}

void test_nested_owned_region() {
    // Nested: outer is OwnedRegion<OR_int_x, tag_y>; outer is detected,
    // and value_type is the inner OwnedRegion (not the inner's int).
    using Nested = ::crucible::safety::OwnedRegion<OR_int_x, test_tag_y>;
    static_assert(extract::is_owned_region_v<Nested>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<Nested>, OR_int_x>);
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<Nested>, test_tag_y>);

    // The inner is also recognizable independently.
    static_assert(extract::is_owned_region_v<
        extract::owned_region_value_t<Nested>>);
}

void test_pointer_to_owned_region_rejected() {
    // remove_cvref does NOT strip pointers — a pointer-to-OwnedRegion
    // is NOT itself an OwnedRegion.  Catches the bug-class where a
    // refactor adds incorrect pointer-decay to the trait.
    using PtrOR = OR_int_x*;
    static_assert(!extract::is_owned_region_v<PtrOR>);
    static_assert(!extract::is_owned_region_v<OR_int_x* const>);
    static_assert(!extract::is_owned_region_v<OR_int_x const*>);
    static_assert(!extract::is_owned_region_v<OR_int_x* const&>);
}

void test_owned_region_with_array_element_type() {
    // Array element types are not commonly used but the trait should
    // not break on them.
    using OR_array = ::crucible::safety::OwnedRegion<int[5], test_tag_x>;
    static_assert(extract::is_owned_region_v<OR_array>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_array>, int[5]>);
}

void test_owned_region_with_pointer_element_type() {
    using OR_ptr = ::crucible::safety::OwnedRegion<int*, test_tag_x>;
    static_assert(extract::is_owned_region_v<OR_ptr>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_ptr>, int*>);
}

void test_volatile_only_qualifier() {
    static_assert(extract::is_owned_region_v<OR_int_x volatile>);
    static_assert(extract::is_owned_region_v<OR_int_x const volatile>);
    static_assert(std::is_same_v<
        extract::owned_region_value_t<OR_int_x volatile>, int>);
    static_assert(std::is_same_v<
        extract::owned_region_tag_t<OR_int_x const volatile>, test_tag_x>);
}

void test_runtime_consistency() {
    // Volatile-bounded loop confirms the predicate is bit-stable.
    volatile std::size_t const cap = 50;
    bool baseline = extract::is_owned_region_v<OR_int_x>;
    EXPECT_TRUE(baseline);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline == extract::is_owned_region_v<OR_int_x>);
        EXPECT_TRUE(!extract::is_owned_region_v<int>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_owned_region:\n");
    run_test("test_runtime_smoke",            test_runtime_smoke);
    run_test("test_positive_cases",           test_positive_cases);
    run_test("test_cvref_stripping",          test_cvref_stripping);
    run_test("test_negative_cases",           test_negative_cases);
    run_test("test_lookalike_rejected",       test_lookalike_rejected);
    run_test("test_concept_form",             test_concept_form);
    run_test("test_value_type_extraction",    test_value_type_extraction);
    run_test("test_tag_extraction",           test_tag_extraction);
    run_test("test_extraction_cvref_stripped", test_extraction_cvref_stripped);
    run_test("test_distinct_specializations", test_distinct_specializations);
    run_test("test_nested_owned_region",      test_nested_owned_region);
    run_test("test_pointer_to_owned_region_rejected",
                                              test_pointer_to_owned_region_rejected);
    run_test("test_owned_region_with_array_element_type",
                                              test_owned_region_with_array_element_type);
    run_test("test_owned_region_with_pointer_element_type",
                                              test_owned_region_with_pointer_element_type);
    run_test("test_volatile_only_qualifier",  test_volatile_only_qualifier);
    run_test("test_runtime_consistency",      test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
