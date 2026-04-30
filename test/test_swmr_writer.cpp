// ═══════════════════════════════════════════════════════════════════
// test_swmr_writer — sentinel TU for safety/SwmrWriter.h
//
// Distinct shape from D15/D16: the SWMR-writer takes a writer
// handle by rvalue ref AND a payload value BY VALUE per §3.6.  The
// concept's discipline is therefore different (param 1 must NOT be
// a reference, must NOT be an OwnedRegion, but otherwise admits any
// non-handle T).
//
// Coverage:
//   * Positive: well-formed match (writer<int> + int value).
//   * Positive: well-formed match with double value.
//   * Positive: value type mismatches handle's payload — admitted
//     by concept, rejected by value_consistent_v.
//   * Positive: non-noexcept publish admitted (D07 has both
//     noexcept and non-noexcept decomp specialisations).
//   * Negative: arity 0, 1, 3.
//   * Negative: handle by lvalue ref / const&&.
//   * Negative: value by lvalue ref / rvalue ref.
//   * Negative: param 0 is not a handle (plain int).
//   * Negative: param 0 is a SwmrReader (D07 disjointness).
//   * Negative: param 0 is a hybrid (publish AND load).
//   * Negative: param 1 is OwnedRegion — overlaps ProducerEndpoint
//     and is explicitly rejected to keep mutual exclusion.
//   * Negative: non-void return.
//   * Cross-shape exclusion vs UnaryTransform / BinaryTransform /
//     ProducerEndpoint / ConsumerEndpoint.
//   * D11 inferred_permission_tags_t harvest: writer's user-tag is
//     NOT an OwnedRegion tag, so D11 returns the empty set.
//   * Volatile&& on handle is admitted.
// ═══════════════════════════════════════════════════════════════════

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

struct in_tag  {};

}  // namespace

namespace sw_test {

// Synthetic SWMR-writer witnesses — must match D07's publish-shape
// (`void publish(P const&) [noexcept]`) AND have NO `load` method
// (the negative requirement).

struct writer_int {
    void publish(int const&) noexcept {}
};

struct writer_double {
    void publish(double const&) noexcept {}
};

// Writer with non-noexcept publish — D07 admits via second decomp
// branch.
struct writer_int_no_noexcept {
    void publish(int const&) {}
};

// Reader-shape — has `load`, rejected by D07's IsSwmrWriter.
struct reader_int {
    int load() const noexcept { return 0; }
};

// Hybrid — has BOTH publish AND load.  Rejected by both D07
// halves (mutual-exclusion negative requirement).
struct hybrid_handle {
    void publish(int const&) noexcept {}
    int  load() const noexcept { return 0; }
};

// Writer-shaped but publish is const-qualified — D07's
// publish_signature_decomp matches `void(C::*)(P const&)` and
// `void(C::*)(P const&) noexcept` only; the const-qualified mptr
// type does not match either, so D07 rejects.
struct writer_const_publish {
    void publish(int const&) const noexcept {}
};

// User-defined struct payload — exercises non-fundamental value type.
struct payload_struct { int a; double b; };

struct writer_struct {
    void publish(payload_struct const&) noexcept {}
};

// Pointer-payload writer — D07 admits because publish_signature_decomp's
// P deduces to `int*`.
struct writer_ptr {
    void publish(int* const&) noexcept {}
};

using OR_int_in = ::crucible::safety::OwnedRegion<int, ::in_tag>;

// ── Positive shapes ─────────────────────────────────────────────

// Canonical §3.6 well-formed: writer<int> + int value, void return.
void f_well_formed(writer_int&&, int) noexcept;

// Different element type.
void f_well_formed_double(writer_double&&, double) noexcept;

// Value type mismatches handle payload.  Concept admits; value_
// consistent_v rejects.
void f_value_mismatch(writer_int&&, double) noexcept;

// Non-noexcept publish writer.
void f_no_noexcept_writer(writer_int_no_noexcept&&, int) noexcept;

// Volatile&& on handle — admitted.
void f_handle_volatile(writer_int volatile&&, int) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

// Arity wrong.
void f_no_param() noexcept;
void f_one_param(writer_int&&) noexcept;
void f_three_params(writer_int&&, int, int) noexcept;

// Handle by lvalue ref / const&&.
void f_handle_lvalue_ref(writer_int&, int) noexcept;
void f_handle_const_rvalue_ref(writer_int const&&, int) noexcept;

// Value by lvalue ref — REJECT (param 1 must be by-value).
void f_value_lvalue_ref(writer_int&&, int&) noexcept;

// Value by rvalue ref — REJECT (param 1 must be by-value).
void f_value_rvalue_ref(writer_int&&, int&&) noexcept;

// Value by const lvalue ref — REJECT.
void f_value_const_lvalue_ref(writer_int&&, int const&) noexcept;

// Param 0 is not a handle.
void f_int_in_handle_slot(int, int) noexcept;

// Param 0 is a SwmrReader — D07 IsSwmrWriter rejects.
void f_reader_in_writer_slot(reader_int&&, int) noexcept;

// Param 0 is hybrid — D07 rejects.
void f_hybrid_in_writer_slot(hybrid_handle&&, int) noexcept;

// Param 1 is OwnedRegion — explicitly rejected to keep mutual
// exclusion with ProducerEndpoint.
void f_region_in_value_slot(writer_int&&, OR_int_in&&) noexcept;

// Non-void return.
int f_int_return(writer_int&&, int) noexcept;

// Writer with const-qualified publish in handle slot — D07 rejects.
void f_const_publish_in_writer_slot(writer_const_publish&&, int) noexcept;

// Const-qualified value param — `int const` is by-value (not a
// reference), so the concept ADMITS.  After cv-strip in
// published_value_t, the type is `int`.
void f_const_value(writer_int&&, int const) noexcept;

// (Volatile-by-value parameters are C++20-deprecated and rejected
// by -Werror=volatile, so we don't witness them here.  cv-stripping
// behaviour is fully covered by the const case above.)

// Pointer-type value param — admitted (pointer is a value type
// per §3.6's literal `T value` wording).  Handle's payload type
// is also int*, so value_consistent_v is true.
void f_pointer_value(writer_ptr&&, int*) noexcept;

// Struct-type value param — exercises non-fundamental T.
void f_struct_value(writer_struct&&, payload_struct) noexcept;

}  // namespace sw_test

namespace {

// ── Tests ────────────────────────────────────────────────────────

void test_runtime_smoke() {
    EXPECT_TRUE(extract::swmr_writer_smoke_test());
}

void test_positive_well_formed() {
    static_assert( extract::SwmrWriter<&sw_test::f_well_formed>);
    static_assert( extract::is_swmr_writer_function_v<
        &sw_test::f_well_formed>);
    static_assert( extract::SwmrWriter<&sw_test::f_well_formed_double>);
}

void test_positive_value_mismatch_admitted_by_concept() {
    // writer_int's payload is int; user passes double — admitted
    // by SHAPE (param 1 is by-value, non-region), rejected by
    // value_consistent_v.
    static_assert( extract::SwmrWriter<&sw_test::f_value_mismatch>);
    static_assert(!extract::swmr_writer_value_consistent_v<
        &sw_test::f_value_mismatch>);
}

void test_positive_non_noexcept_publish() {
    static_assert( extract::SwmrWriter<&sw_test::f_no_noexcept_writer>);
}

void test_positive_volatile_handle_admitted() {
    static_assert( extract::SwmrWriter<&sw_test::f_handle_volatile>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::SwmrWriter<&sw_test::f_no_param>);
    static_assert(!extract::SwmrWriter<&sw_test::f_one_param>);
    static_assert(!extract::SwmrWriter<&sw_test::f_three_params>);
}

void test_negative_handle_not_rvalue_ref() {
    static_assert(!extract::SwmrWriter<&sw_test::f_handle_lvalue_ref>);
}

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::SwmrWriter<
        &sw_test::f_handle_const_rvalue_ref>);
}

void test_negative_value_by_reference() {
    // §3.6 specifies `T value` (by-value); reject any reference shape.
    static_assert(!extract::SwmrWriter<&sw_test::f_value_lvalue_ref>);
    static_assert(!extract::SwmrWriter<&sw_test::f_value_rvalue_ref>);
    static_assert(!extract::SwmrWriter<&sw_test::f_value_const_lvalue_ref>);
}

void test_negative_handle_slot_not_handle() {
    static_assert(!extract::SwmrWriter<&sw_test::f_int_in_handle_slot>);
}

void test_negative_reader_in_writer_slot() {
    // D07's IsSwmrWriter requires publish presence + load absence.
    // A reader has load → IsSwmrWriter rejects → SwmrWriter rejects.
    static_assert(!extract::SwmrWriter<&sw_test::f_reader_in_writer_slot>);
}

void test_negative_hybrid_in_writer_slot() {
    static_assert(!extract::SwmrWriter<&sw_test::f_hybrid_in_writer_slot>);
}

void test_negative_const_qualified_publish() {
    // D07's publish_signature_decomp matches mptr types WITHOUT
    // cv-ref qualifiers on the method.  A const-qualified publish
    // produces `void (C::*)(int const&) const [noexcept]`, which
    // does not match either decomp specialisation.  D07 rejects
    // → D17 inherits the rejection.
    static_assert(!extract::SwmrWriter<
        &sw_test::f_const_publish_in_writer_slot>);
}

void test_positive_cv_qualified_value_admitted() {
    // `int const` is by-value (not a reference), so the concept
    // ADMITS.  published_value_t cv-strips to `int`.
    // (Volatile-by-value is C++20-deprecated and rejected by
    // -Werror=volatile, so we don't witness it.)
    static_assert( extract::SwmrWriter<&sw_test::f_const_value>);

    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<&sw_test::f_const_value>,
        int>);

    // Value-consistent with handle (writer_int has int payload).
    static_assert(extract::swmr_writer_value_consistent_v<
        &sw_test::f_const_value>);
}

void test_positive_pointer_value() {
    // §3.6's `T value` wording admits pointer types — they are
    // by-value too (the pointer itself is the value).  writer_ptr
    // declares publish(int* const&); D07 deduces P = int*.
    static_assert( extract::SwmrWriter<&sw_test::f_pointer_value>);

    static_assert(std::is_same_v<
        extract::swmr_writer_handle_value_t<&sw_test::f_pointer_value>,
        int*>);
    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<&sw_test::f_pointer_value>,
        int*>);
    static_assert(extract::swmr_writer_value_consistent_v<
        &sw_test::f_pointer_value>);
}

void test_positive_struct_value() {
    // Non-fundamental value type — exercises the deduction path
    // for user-defined types.
    static_assert( extract::SwmrWriter<&sw_test::f_struct_value>);

    static_assert(std::is_same_v<
        extract::swmr_writer_handle_value_t<&sw_test::f_struct_value>,
        sw_test::payload_struct>);
    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<&sw_test::f_struct_value>,
        sw_test::payload_struct>);
}

void test_negative_owned_region_in_value_slot() {
    // OwnedRegion in value slot would semantically mean "publish a
    // contiguous run of values" — that's NOT what SWMR publish
    // does.  Also overlaps ProducerEndpoint structurally.  Reject.
    static_assert(!extract::SwmrWriter<&sw_test::f_region_in_value_slot>);
}

void test_negative_non_void_return() {
    static_assert(!extract::SwmrWriter<&sw_test::f_int_return>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<
        extract::swmr_writer_handle_value_t<&sw_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::swmr_writer_handle_value_t<&sw_test::f_well_formed_double>,
        double>);
    static_assert(std::is_same_v<
        extract::swmr_writer_handle_value_t<&sw_test::f_value_mismatch>,
        int>);
}

void test_published_value_extraction() {
    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<&sw_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<
            &sw_test::f_well_formed_double>,
        double>);
    // Mismatch: published_value is double, handle_value is int.
    static_assert(std::is_same_v<
        extract::swmr_writer_published_value_t<&sw_test::f_value_mismatch>,
        double>);
}

void test_value_consistency_predicate() {
    static_assert(extract::swmr_writer_value_consistent_v<
        &sw_test::f_well_formed>);
    static_assert(extract::swmr_writer_value_consistent_v<
        &sw_test::f_well_formed_double>);
    static_assert(!extract::swmr_writer_value_consistent_v<
        &sw_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    auto callable_with_swmr = []<auto FnPtr>()
        requires extract::SwmrWriter<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_swmr.template operator()<
        &sw_test::f_well_formed>());
    EXPECT_TRUE(callable_with_swmr.template operator()<
        &sw_test::f_well_formed_double>());
}

void test_cross_shape_exclusion_full_matrix() {
    // SwmrWriter must NOT collide with any other canonical shape.
    // f_well_formed has shape (Writer&&, int) — no OwnedRegion, no
    // consumer/producer handle.

    static_assert( extract::SwmrWriter<&sw_test::f_well_formed>);
    static_assert(!extract::ProducerEndpoint<&sw_test::f_well_formed>);
    static_assert(!extract::ConsumerEndpoint<&sw_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&sw_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&sw_test::f_well_formed>);

    // Region-in-value-slot signature is rejected by SwmrWriter
    // (explicitly) — it would otherwise overlap ProducerEndpoint
    // structurally if we relaxed that check.
    static_assert(!extract::SwmrWriter<&sw_test::f_region_in_value_slot>);
}

void test_inferred_tags_does_not_harvest_handle_tag() {
    // D11 inferred_permission_tags_t harvests OwnedRegion tags only.
    // A SwmrWriter signature has no OwnedRegion parameter, so the
    // harvested set is EMPTY.  The dispatcher will mint
    // Writer<UserTag> permission separately at lowering time —
    // that tag does NOT come from D11.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&sw_test::f_well_formed>,
        Expected>);
    static_assert(extract::inferred_permission_tags_count_v<
        &sw_test::f_well_formed> == 0);

    // Tag-free check confirms.
    static_assert(extract::is_tag_free_function_v<
        &sw_test::f_well_formed>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos =
        extract::is_swmr_writer_function_v<&sw_test::f_well_formed>;
    bool baseline_neg =
        !extract::is_swmr_writer_function_v<&sw_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_swmr_writer_function_v<&sw_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_swmr_writer_function_v<&sw_test::f_one_param>);
        EXPECT_TRUE(extract::SwmrWriter<&sw_test::f_well_formed>);
        EXPECT_TRUE(!extract::SwmrWriter<&sw_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_swmr_writer:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed",
             test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted_by_concept",
             test_positive_value_mismatch_admitted_by_concept);
    run_test("test_positive_non_noexcept_publish",
             test_positive_non_noexcept_publish);
    run_test("test_positive_volatile_handle_admitted",
             test_positive_volatile_handle_admitted);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref",
             test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref",
             test_negative_handle_const_rvalue_ref);
    run_test("test_negative_value_by_reference",
             test_negative_value_by_reference);
    run_test("test_negative_handle_slot_not_handle",
             test_negative_handle_slot_not_handle);
    run_test("test_negative_reader_in_writer_slot",
             test_negative_reader_in_writer_slot);
    run_test("test_negative_hybrid_in_writer_slot",
             test_negative_hybrid_in_writer_slot);
    run_test("test_negative_const_qualified_publish",
             test_negative_const_qualified_publish);
    run_test("test_positive_cv_qualified_value_admitted",
             test_positive_cv_qualified_value_admitted);
    run_test("test_positive_pointer_value",
             test_positive_pointer_value);
    run_test("test_positive_struct_value",
             test_positive_struct_value);
    run_test("test_negative_owned_region_in_value_slot",
             test_negative_owned_region_in_value_slot);
    run_test("test_negative_non_void_return",
             test_negative_non_void_return);
    run_test("test_handle_value_extraction",
             test_handle_value_extraction);
    run_test("test_published_value_extraction",
             test_published_value_extraction);
    run_test("test_value_consistency_predicate",
             test_value_consistency_predicate);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_full_matrix",
             test_cross_shape_exclusion_full_matrix);
    run_test("test_inferred_tags_does_not_harvest_handle_tag",
             test_inferred_tags_does_not_harvest_handle_tag);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
