#include <crucible/safety/SwmrReader.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
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

struct out_tag {};

}  // namespace

namespace sr_test {

// A reader witness has a const-qualified load and no publish.

struct reader_int {
    int load() const noexcept { return 0; }
};

struct reader_double {
    double load() const noexcept { return 0.0; }
};

struct reader_int_no_noexcept {
    int load() const { return 0; }
};

struct writer_int {
    void publish(int const&) noexcept {}
};

struct hybrid_handle {
    void publish(int const&) noexcept {}
    int load() const noexcept { return 0; }
};

struct reader_non_const_load {
    int load() noexcept { return 0; }
};

struct reader_void_load {
    void load() const noexcept {}
};

struct reader_with_extras {
    int load() const noexcept { return 0; }
    void unrelated_helper() noexcept {}
    int another_method(int) const noexcept { return 0; }
};

struct reader_optional_load {
    std::optional<int> load() const noexcept { return 0; }
};

struct payload_struct {
    int a;
    double b;
};
struct reader_struct {
    payload_struct load() const noexcept { return {}; }
};

struct reader_ptr {
    int* load() const noexcept { return nullptr; }
};

using OR_int_out = ::crucible::safety::OwnedRegion<int, ::out_tag>;

int f_well_formed(reader_int&&) noexcept;

double f_well_formed_double(reader_double&&) noexcept;

double f_value_mismatch(reader_int&&) noexcept;

int f_no_noexcept_reader(reader_int_no_noexcept&&) noexcept;

// A top-level const on a by-value return is ignored by the language and
// trips -Werror=ignored-qualifiers, so there is no const-return witness
// here.  The cv-strip path is covered on the writer side instead.

int* f_pointer_return(reader_ptr&&) noexcept;

payload_struct f_struct_return(reader_struct&&) noexcept;

int f_handle_volatile(reader_int volatile&&) noexcept;

void f_no_param_no_return() noexcept;
int f_no_param_int_return() noexcept;
int f_two_params(reader_int&&, int) noexcept;

int f_handle_lvalue_ref(reader_int&) noexcept;
int f_handle_const_rvalue_ref(reader_int const&&) noexcept;

void f_void_return(reader_int&&) noexcept;

int& f_lvalue_ref_return(reader_int&&) noexcept;
int&& f_rvalue_ref_return(reader_int&&) noexcept;

OR_int_out f_region_return(reader_int&&) noexcept;

int f_int_in_handle_slot(int) noexcept;

int f_writer_in_reader_slot(writer_int&&) noexcept;

int f_hybrid_in_reader_slot(hybrid_handle&&) noexcept;

int f_non_const_load_in_reader_slot(reader_non_const_load&&) noexcept;

int f_void_load_in_reader_slot(reader_void_load&&) noexcept;

int f_reader_with_extras(reader_with_extras&&) noexcept;

std::optional<int> f_optional_reader(reader_optional_load&&) noexcept;

// The same tag on input and output is a valid unary-transform shape.
OR_int_out f_unary_transform_witness(OR_int_out&&) noexcept;

}  // namespace sr_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::swmr_reader_smoke_test()); }

void test_positive_well_formed() {
    static_assert(extract::SwmrReader<&sr_test::f_well_formed>);
    static_assert(extract::is_swmr_reader_function_v<&sr_test::f_well_formed>);
    static_assert(extract::SwmrReader<&sr_test::f_well_formed_double>);
}

void test_positive_value_mismatch_admitted() {
    static_assert(extract::SwmrReader<&sr_test::f_value_mismatch>);
    static_assert(!extract::swmr_reader_value_consistent_v<&sr_test::f_value_mismatch>);
}

void test_positive_non_noexcept_load() { static_assert(extract::SwmrReader<&sr_test::f_no_noexcept_reader>); }

void test_positive_pointer_return() {
    static_assert(extract::SwmrReader<&sr_test::f_pointer_return>);
    static_assert(std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_pointer_return>, int*>);
    static_assert(std::is_same_v<extract::swmr_reader_returned_value_t<&sr_test::f_pointer_return>, int*>);
}

void test_positive_struct_return() {
    static_assert(extract::SwmrReader<&sr_test::f_struct_return>);
    static_assert(
        std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_struct_return>, sr_test::payload_struct>);
}

void test_positive_volatile_handle_admitted() { static_assert(extract::SwmrReader<&sr_test::f_handle_volatile>); }

void test_negative_arity_mismatch() {
    static_assert(!extract::SwmrReader<&sr_test::f_no_param_no_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_no_param_int_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_two_params>);
}

void test_negative_handle_not_rvalue_ref() { static_assert(!extract::SwmrReader<&sr_test::f_handle_lvalue_ref>); }

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::SwmrReader<&sr_test::f_handle_const_rvalue_ref>);
}

void test_negative_void_return() { static_assert(!extract::SwmrReader<&sr_test::f_void_return>); }

void test_negative_reference_return() {
    // Returning a reference exposes state the writer can change mid-read.
    static_assert(!extract::SwmrReader<&sr_test::f_lvalue_ref_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_rvalue_ref_return>);
}

void test_negative_owned_region_return() { static_assert(!extract::SwmrReader<&sr_test::f_region_return>); }

void test_negative_handle_slot_not_handle() { static_assert(!extract::SwmrReader<&sr_test::f_int_in_handle_slot>); }

void test_negative_writer_in_reader_slot() { static_assert(!extract::SwmrReader<&sr_test::f_writer_in_reader_slot>); }

void test_negative_hybrid_in_reader_slot() { static_assert(!extract::SwmrReader<&sr_test::f_hybrid_in_reader_slot>); }

void test_negative_non_const_load() { static_assert(!extract::SwmrReader<&sr_test::f_non_const_load_in_reader_slot>); }

void test_negative_void_load() { static_assert(!extract::SwmrReader<&sr_test::f_void_load_in_reader_slot>); }

void test_positive_handle_with_extras_admitted() {
    // Detection is duck-typed: extra unrelated methods do not break it.
    static_assert(extract::SwmrReader<&sr_test::f_reader_with_extras>);
    static_assert(std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_reader_with_extras>, int>);
}

void test_optional_returning_load_admitted() {
    // This pins behaviour that the header's own comment argues against:
    // an optional-returning load reads as a non-blocking try-load, a
    // different shape, yet nothing rejects it.  Tightening the payload
    // deduction later turns this test red, which is the point.
    static_assert(extract::SwmrReader<&sr_test::f_optional_reader>);
    static_assert(std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_optional_reader>, std::optional<int>>);
    static_assert(
        std::is_same_v<extract::swmr_reader_returned_value_t<&sr_test::f_optional_reader>, std::optional<int>>);
    static_assert(extract::swmr_reader_value_consistent_v<&sr_test::f_optional_reader>);
}

void test_unary_transform_witness_not_swmr_reader() {
    static_assert(extract::UnaryTransform<&sr_test::f_unary_transform_witness>);
    static_assert(!extract::SwmrReader<&sr_test::f_unary_transform_witness>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::swmr_reader_handle_value_t<&sr_test::f_well_formed_double>, double>);
}

void test_returned_value_extraction() {
    static_assert(std::is_same_v<extract::swmr_reader_returned_value_t<&sr_test::f_well_formed>, int>);
    static_assert(std::is_same_v<extract::swmr_reader_returned_value_t<&sr_test::f_value_mismatch>, double>);
}

void test_value_consistency_predicate() {
    static_assert(extract::swmr_reader_value_consistent_v<&sr_test::f_well_formed>);
    static_assert(extract::swmr_reader_value_consistent_v<&sr_test::f_well_formed_double>);
    static_assert(!extract::swmr_reader_value_consistent_v<&sr_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    auto callable_with_swmr = []<auto FnPtr>()
        requires extract::SwmrReader<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_swmr.template operator()<&sr_test::f_well_formed>());
    EXPECT_TRUE(callable_with_swmr.template operator()<&sr_test::f_well_formed_double>());
}

void test_cross_shape_exclusion_full_matrix() {
    // Arity 1 with a non-void return also describes an out-of-place
    // unary transform.  Only the type in slot 0 separates them.

    static_assert(extract::SwmrReader<&sr_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&sr_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&sr_test::f_well_formed>);
    static_assert(!extract::ProducerEndpoint<&sr_test::f_well_formed>);
    static_assert(!extract::ConsumerEndpoint<&sr_test::f_well_formed>);
    static_assert(!extract::SwmrWriter<&sr_test::f_well_formed>);

    static_assert(!extract::SwmrReader<&sr_test::f_region_return>);
}

void test_inferred_tags_does_not_harvest_handle_tag() {
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<extract::inferred_permission_tags_t<&sr_test::f_well_formed>, Expected>);
    static_assert(extract::inferred_permission_tags_count_v<&sr_test::f_well_formed> == 0);
    static_assert(extract::is_tag_free_function_v<&sr_test::f_well_formed>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_swmr_reader_function_v<&sr_test::f_well_formed>;
    bool baseline_neg = !extract::is_swmr_reader_function_v<&sr_test::f_no_param_no_return>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_swmr_reader_function_v<&sr_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg == !extract::is_swmr_reader_function_v<&sr_test::f_no_param_no_return>);
        EXPECT_TRUE(extract::SwmrReader<&sr_test::f_well_formed>);
        EXPECT_TRUE(!extract::SwmrReader<&sr_test::f_no_param_no_return>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_swmr_reader:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed", test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted", test_positive_value_mismatch_admitted);
    run_test("test_positive_non_noexcept_load", test_positive_non_noexcept_load);
    run_test("test_positive_pointer_return", test_positive_pointer_return);
    run_test("test_positive_struct_return", test_positive_struct_return);
    run_test("test_positive_volatile_handle_admitted", test_positive_volatile_handle_admitted);
    run_test("test_negative_arity_mismatch", test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref", test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref", test_negative_handle_const_rvalue_ref);
    run_test("test_negative_void_return", test_negative_void_return);
    run_test("test_negative_reference_return", test_negative_reference_return);
    run_test("test_negative_owned_region_return", test_negative_owned_region_return);
    run_test("test_negative_handle_slot_not_handle", test_negative_handle_slot_not_handle);
    run_test("test_negative_writer_in_reader_slot", test_negative_writer_in_reader_slot);
    run_test("test_negative_hybrid_in_reader_slot", test_negative_hybrid_in_reader_slot);
    run_test("test_negative_non_const_load", test_negative_non_const_load);
    run_test("test_negative_void_load", test_negative_void_load);
    run_test("test_positive_handle_with_extras_admitted", test_positive_handle_with_extras_admitted);
    run_test("test_optional_returning_load_admitted", test_optional_returning_load_admitted);
    run_test("test_unary_transform_witness_not_swmr_reader", test_unary_transform_witness_not_swmr_reader);
    run_test("test_handle_value_extraction", test_handle_value_extraction);
    run_test("test_returned_value_extraction", test_returned_value_extraction);
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
