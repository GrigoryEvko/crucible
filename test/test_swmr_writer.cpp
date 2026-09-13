#include <crucible/safety/SwmrWriter.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/UnaryTransform.h>

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
namespace safety = ::crucible::safety;

struct in_tag {};

}  // namespace

namespace sw_test {

// A writer witness has publish and no load.

struct writer_int {
    void publish(int const&) noexcept {}
};

struct writer_double {
    void publish(double const&) noexcept {}
};

struct writer_int_no_noexcept {
    void publish(int const&) {}
};

struct reader_int {
    int load() const noexcept { return 0; }
};

struct hybrid_handle {
    void publish(int const&) noexcept {}
    int load() const noexcept { return 0; }
};

struct writer_const_publish {
    void publish(int const&) const noexcept {}
};

struct payload_struct {
    int a;
    double b;
};

struct writer_struct {
    void publish(payload_struct const&) noexcept {}
};

struct writer_ptr {
    void publish(int* const&) noexcept {}
};

using OR_int_in = ::crucible::safety::OwnedRegion<int, ::in_tag>;

void f_well_formed(writer_int&&, int) noexcept;

void f_well_formed_double(writer_double&&, double) noexcept;

void f_value_mismatch(writer_int&&, double) noexcept;

void f_no_noexcept_writer(writer_int_no_noexcept&&, int) noexcept;

void f_handle_volatile(writer_int volatile&&, int) noexcept;

void f_no_param() noexcept;
void f_one_param(writer_int&&) noexcept;
void f_three_params(writer_int&&, int, int) noexcept;

void f_handle_lvalue_ref(writer_int&, int) noexcept;
void f_handle_const_rvalue_ref(writer_int const&&, int) noexcept;

void f_value_lvalue_ref(writer_int&&, int&) noexcept;

void f_value_rvalue_ref(writer_int&&, int&&) noexcept;

void f_value_const_lvalue_ref(writer_int&&, int const&) noexcept;

void f_int_in_handle_slot(int, int) noexcept;

void f_reader_in_writer_slot(reader_int&&, int) noexcept;

void f_hybrid_in_writer_slot(hybrid_handle&&, int) noexcept;

void f_region_in_value_slot(writer_int&&, OR_int_in&&) noexcept;

int f_int_return(writer_int&&, int) noexcept;

void f_const_publish_in_writer_slot(writer_const_publish&&, int) noexcept;

void f_const_value(writer_int&&, int const) noexcept;

// Volatile by-value parameters are deprecated and rejected by
// -Werror=volatile, so there is no volatile witness here.

void f_pointer_value(writer_ptr&&, int*) noexcept;

void f_struct_value(writer_struct&&, payload_struct) noexcept;

}  // namespace sw_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::swmr_writer_smoke_test()); }

void test_positive_well_formed() {
    static_assert(extract::SwmrWriter<&sw_test::f_well_formed>);
    static_assert(extract::is_swmr_writer_function_v<&sw_test::f_well_formed>);
    static_assert(extract::SwmrWriter<&sw_test::f_well_formed_double>);
}

void test_positive_value_mismatch_admitted_by_concept() {
    // The shape check never compares the two types, so the mismatch is
    // admitted here and caught by the consistency predicate.
    static_assert(extract::SwmrWriter<&sw_test::f_value_mismatch>);
    static_assert(!extract::swmr_writer_value_consistent_v<&sw_test::f_value_mismatch>);
}

void test_positive_non_noexcept_publish() { static_assert(extract::SwmrWriter<&sw_test::f_no_noexcept_writer>); }

void test_positive_volatile_handle_admitted() { static_assert(extract::SwmrWriter<&sw_test::f_handle_volatile>); }

void test_negative_arity_mismatch() {
    static_assert(!extract::SwmrWriter<&sw_test::f_no_param>);
    static_assert(!extract::SwmrWriter<&sw_test::f_one_param>);
    static_assert(!extract::SwmrWriter<&sw_test::f_three_params>);
}

void test_negative_handle_not_rvalue_ref() { static_assert(!extract::SwmrWriter<&sw_test::f_handle_lvalue_ref>); }

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::SwmrWriter<&sw_test::f_handle_const_rvalue_ref>);
}

void test_negative_value_by_reference() {
    // The value parameter is by value, so any reference shape fails.
    static_assert(!extract::SwmrWriter<&sw_test::f_value_lvalue_ref>);
    static_assert(!extract::SwmrWriter<&sw_test::f_value_rvalue_ref>);
    static_assert(!extract::SwmrWriter<&sw_test::f_value_const_lvalue_ref>);
}

void test_negative_handle_slot_not_handle() { static_assert(!extract::SwmrWriter<&sw_test::f_int_in_handle_slot>); }

void test_negative_reader_in_writer_slot() {
    // A writer must have publish and must not have load.
    static_assert(!extract::SwmrWriter<&sw_test::f_reader_in_writer_slot>);
}

void test_negative_hybrid_in_writer_slot() { static_assert(!extract::SwmrWriter<&sw_test::f_hybrid_in_writer_slot>); }

void test_negative_const_qualified_publish() {
    // Rejected, unlike the non-noexcept witness above.
    static_assert(!extract::SwmrWriter<&sw_test::f_const_publish_in_writer_slot>);
}

void test_positive_cv_qualified_value_admitted() {
    // A top-level const is still by value, so this admits and the
    // published type strips to int.
    static_assert(extract::SwmrWriter<&sw_test::f_const_value>);

    static_assert(std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_const_value>, int>);

    static_assert(extract::swmr_writer_value_consistent_v<&sw_test::f_const_value>);
}

void test_positive_pointer_value() {
    // A pointer is a value type here: the pointer itself is the value.
    static_assert(extract::SwmrWriter<&sw_test::f_pointer_value>);

    static_assert(std::is_same_v<extract::swmr_writer_handle_value_t<&sw_test::f_pointer_value>, int*>);
    static_assert(std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_pointer_value>, int*>);
    static_assert(extract::swmr_writer_value_consistent_v<&sw_test::f_pointer_value>);
}

void test_positive_struct_value() {
    static_assert(extract::SwmrWriter<&sw_test::f_struct_value>);

    static_assert(
        std::is_same_v<extract::swmr_writer_handle_value_t<&sw_test::f_struct_value>, sw_test::payload_struct>);
    static_assert(
        std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_struct_value>, sw_test::payload_struct>);
}

void test_negative_owned_region_in_value_slot() {
    // A region in the value slot would mean publishing a run of values,
    // which is not what a single publish does, and it would overlap the
    // producer-endpoint shape.
    static_assert(!extract::SwmrWriter<&sw_test::f_region_in_value_slot>);
}

void test_negative_non_void_return() { static_assert(!extract::SwmrWriter<&sw_test::f_int_return>); }

void test_handle_value_extraction() {
    static_assert(std::is_same_v<extract::swmr_writer_handle_value_t<&sw_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::swmr_writer_handle_value_t<&sw_test::f_well_formed_double>, double>);
    static_assert(std::is_same_v<extract::swmr_writer_handle_value_t<&sw_test::f_value_mismatch>, int>);
}

void test_published_value_extraction() {
    static_assert(std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_well_formed_double>, double>);
    static_assert(std::is_same_v<extract::swmr_writer_published_value_t<&sw_test::f_value_mismatch>, double>);
}

void test_value_consistency_predicate() {
    static_assert(extract::swmr_writer_value_consistent_v<&sw_test::f_well_formed>);
    static_assert(extract::swmr_writer_value_consistent_v<&sw_test::f_well_formed_double>);
    static_assert(!extract::swmr_writer_value_consistent_v<&sw_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    auto callable_with_swmr = []<auto FnPtr>()
        requires extract::SwmrWriter<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_swmr.template operator()<&sw_test::f_well_formed>());
    EXPECT_TRUE(callable_with_swmr.template operator()<&sw_test::f_well_formed_double>());
}

void test_cross_shape_exclusion_full_matrix() {
    static_assert(extract::SwmrWriter<&sw_test::f_well_formed>);
    static_assert(!extract::ProducerEndpoint<&sw_test::f_well_formed>);
    static_assert(!extract::ConsumerEndpoint<&sw_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&sw_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&sw_test::f_well_formed>);

    static_assert(!extract::SwmrWriter<&sw_test::f_region_in_value_slot>);
}

void test_inferred_tags_does_not_harvest_handle_tag() {
    // The signature has no OwnedRegion, so the harvested set is empty.
    // The writer's own permission tag is minted elsewhere and never
    // appears here.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&sw_test::f_well_formed>, Expected>);
    static_assert(extract::inferred_permission_tags_count_v<&sw_test::f_well_formed> == 0);

    static_assert(extract::is_tag_free_function_v<&sw_test::f_well_formed>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_swmr_writer_function_v<&sw_test::f_well_formed>;
    bool baseline_neg = !extract::is_swmr_writer_function_v<&sw_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_swmr_writer_function_v<&sw_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg == !extract::is_swmr_writer_function_v<&sw_test::f_one_param>);
        EXPECT_TRUE(extract::SwmrWriter<&sw_test::f_well_formed>);
        EXPECT_TRUE(!extract::SwmrWriter<&sw_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_swmr_writer:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed", test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted_by_concept", test_positive_value_mismatch_admitted_by_concept);
    run_test("test_positive_non_noexcept_publish", test_positive_non_noexcept_publish);
    run_test("test_positive_volatile_handle_admitted", test_positive_volatile_handle_admitted);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref", test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref", test_negative_handle_const_rvalue_ref);
    run_test("test_negative_value_by_reference", test_negative_value_by_reference);
    run_test("test_negative_handle_slot_not_handle", test_negative_handle_slot_not_handle);
    run_test("test_negative_reader_in_writer_slot", test_negative_reader_in_writer_slot);
    run_test("test_negative_hybrid_in_writer_slot", test_negative_hybrid_in_writer_slot);
    run_test("test_negative_const_qualified_publish", test_negative_const_qualified_publish);
    run_test("test_positive_cv_qualified_value_admitted", test_positive_cv_qualified_value_admitted);
    run_test("test_positive_pointer_value", test_positive_pointer_value);
    run_test("test_positive_struct_value", test_positive_struct_value);
    run_test("test_negative_owned_region_in_value_slot", test_negative_owned_region_in_value_slot);
    run_test("test_negative_non_void_return", test_negative_non_void_return);
    run_test("test_handle_value_extraction", test_handle_value_extraction);
    run_test("test_published_value_extraction", test_published_value_extraction);
    run_test("test_value_consistency_predicate", test_value_consistency_predicate);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_full_matrix", test_cross_shape_exclusion_full_matrix);
    run_test("test_inferred_tags_does_not_harvest_handle_tag", test_inferred_tags_does_not_harvest_handle_tag);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
