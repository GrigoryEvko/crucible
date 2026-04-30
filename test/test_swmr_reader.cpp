// ═══════════════════════════════════════════════════════════════════
// test_swmr_reader — sentinel TU for safety/SwmrReader.h
//
// Distinct shape from D15/D16/D17: arity 1, non-void return, no
// region or value parameter beyond the handle.  Per §3.6:
//
//   T reader(SwmrReaderHandle&&)
//
// Coverage:
//   * Positive: well-formed match (reader<int> → int).
//   * Positive: well-formed match with double, struct, pointer.
//   * Positive: return type mismatches handle's load type — admitted
//     by concept, rejected by value_consistent_v.
//   * Positive: const-qualified return — admitted (cv-strip on
//     returned_value_t).
//   * Positive: non-noexcept load admitted (D07 second-decomp branch).
//   * Negative: arity 0, 2.
//   * Negative: handle by lvalue ref / const&&.
//   * Negative: void return — that's not a load.
//   * Negative: reference return — torn-state risk per §3.6.
//   * Negative: OwnedRegion return — different shape.
//   * Negative: param 0 is not a handle.
//   * Negative: param 0 is a SwmrWriter (D07 disjointness).
//   * Negative: param 0 is hybrid (publish AND load).
//   * Negative: param 0 is reader-shaped but load is non-const (D07
//     load_signature_decomp matches const-qualified only).
//   * Negative: param 0 has load returning void — D07 explicitly
//     rejects (load_shape filters !is_void on payload).
//   * Cross-shape exclusion: arity 1 + non-void return overlaps
//     UnaryTransform's out-of-place case syntactically — confirm
//     the per-clause exclusion (handle vs OwnedRegion in param 0).
//   * D11 inferred_permission_tags_t: empty set.
//   * Volatile&& on handle admitted.
// ═══════════════════════════════════════════════════════════════════

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

struct out_tag {};

}  // namespace

namespace sr_test {

// Synthetic SWMR-reader witnesses — must match D07's load-shape
// (`P load() const [noexcept]`) AND have NO `publish` method.

struct reader_int {
    int load() const noexcept { return 0; }
};

struct reader_double {
    double load() const noexcept { return 0.0; }
};

// Non-noexcept load — D07 admits via second decomp branch.
struct reader_int_no_noexcept {
    int load() const { return 0; }
};

// Writer — has publish, no load.  Rejected by D07's IsSwmrReader.
struct writer_int {
    void publish(int const&) noexcept {}
};

// Hybrid — both publish AND load.  Both halves of D07 reject.
struct hybrid_handle {
    void publish(int const&) noexcept {}
    int  load() const noexcept { return 0; }
};

// Reader-shape but load is NON-const-qualified.  D07's
// load_signature_decomp matches `P (C::*)() const [noexcept]` only,
// so D07 rejects.
struct reader_non_const_load {
    int load() noexcept { return 0; }
};

// Reader-shape but load returns void.  D07's load_shape::matches
// is `decomp::matches && !std::is_void_v<payload>` — rejects.
struct reader_void_load {
    void load() const noexcept {}
};

// Reader-shape with EXTRA unrelated methods.  D07 detects only
// publish/load; extras are structurally invisible.  D18 inherits.
struct reader_with_extras {
    int  load() const noexcept { return 0; }
    void unrelated_helper() noexcept {}
    int  another_method(int) const noexcept { return 0; }
};

// Reader returning std::optional<P> — D07 admits this because
// load_signature_decomp's P deduces to `optional<int>` (which is
// non-void).  The IsSwmrHandle.h header comment at L80-82 says
// optional<T> returns "would signal a non-blocking try-load... a
// different shape" — but that intent is NOT enforced; the current
// implementation accepts.  D18 inherits the acceptance.  This
// witness pins the current behaviour so any future tightening of
// D07 is detected via a CI red.
struct reader_optional_load {
    std::optional<int> load() const noexcept { return 0; }
};

// User-defined struct payload.
struct payload_struct { int a; double b; };
struct reader_struct {
    payload_struct load() const noexcept { return {}; }
};

// Pointer-payload reader.
struct reader_ptr {
    int* load() const noexcept { return nullptr; }
};

using OR_int_out = ::crucible::safety::OwnedRegion<int, ::out_tag>;

// ── Positive shapes ─────────────────────────────────────────────

// Canonical §3.6 well-formed: reader<int> → int.
int f_well_formed(reader_int&&) noexcept;

// Different element type.
double f_well_formed_double(reader_double&&) noexcept;

// Return type mismatches handle payload (reader_int's load returns
// int; this function returns double).  Concept admits; value_
// consistent_v rejects.
double f_value_mismatch(reader_int&&) noexcept;

// Non-noexcept load admitted.
int f_no_noexcept_reader(reader_int_no_noexcept&&) noexcept;

// (Top-level cv on by-value return types is ignored by C++ and
// triggers -Werror=ignored-qualifiers, so we don't witness `int
// const` returns.  cv-strip behaviour is exercised on the cv-
// qualified value parameter case in test_swmr_writer.cpp.)

// Pointer return — admitted (`int*` is non-reference).
int* f_pointer_return(reader_ptr&&) noexcept;

// Struct return — exercises non-fundamental T.
payload_struct f_struct_return(reader_struct&&) noexcept;

// Volatile&& on handle — admitted.
int f_handle_volatile(reader_int volatile&&) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

// Arity wrong.
void f_no_param_no_return() noexcept;
int  f_no_param_int_return() noexcept;
int  f_two_params(reader_int&&, int) noexcept;

// Handle by lvalue ref / const&&.
int f_handle_lvalue_ref(reader_int&) noexcept;
int f_handle_const_rvalue_ref(reader_int const&&) noexcept;

// Void return — rejects.
void f_void_return(reader_int&&) noexcept;

// Reference return — rejects (torn-state risk per §3.6).
int& f_lvalue_ref_return(reader_int&&) noexcept;
int&& f_rvalue_ref_return(reader_int&&) noexcept;

// OwnedRegion return — different shape.
OR_int_out f_region_return(reader_int&&) noexcept;

// Param 0 is not a handle.
int f_int_in_handle_slot(int) noexcept;

// Param 0 is a SwmrWriter — D07 IsSwmrReader rejects.
int f_writer_in_reader_slot(writer_int&&) noexcept;

// Param 0 is hybrid — D07 rejects.
int f_hybrid_in_reader_slot(hybrid_handle&&) noexcept;

// Param 0 is reader-shaped but load is non-const — D07 rejects.
int f_non_const_load_in_reader_slot(reader_non_const_load&&) noexcept;

// Param 0 is reader-shaped but load returns void — D07 rejects.
int f_void_load_in_reader_slot(reader_void_load&&) noexcept;

// Reader with extra unrelated methods — admitted; extras are
// structurally invisible to D07.
int f_reader_with_extras(reader_with_extras&&) noexcept;

// Reader returning optional<int> — admitted (current behaviour;
// see witness comment above).  Both handle_value_t and
// returned_value_t are `std::optional<int>`.
std::optional<int> f_optional_reader(reader_optional_load&&) noexcept;

// True UnaryTransform signature — confirms the cross-shape
// exclusion direction we DIDN'T cover yet: a known UnaryTransform
// must NOT be a SwmrReader.  (Same tag on input and output is a
// valid UnaryTransform shape.)
OR_int_out f_unary_transform_witness(OR_int_out&&) noexcept;

}  // namespace sr_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::swmr_reader_smoke_test());
}

void test_positive_well_formed() {
    static_assert( extract::SwmrReader<&sr_test::f_well_formed>);
    static_assert( extract::is_swmr_reader_function_v<
        &sr_test::f_well_formed>);
    static_assert( extract::SwmrReader<&sr_test::f_well_formed_double>);
}

void test_positive_value_mismatch_admitted() {
    static_assert( extract::SwmrReader<&sr_test::f_value_mismatch>);
    static_assert(!extract::swmr_reader_value_consistent_v<
        &sr_test::f_value_mismatch>);
}

void test_positive_non_noexcept_load() {
    static_assert( extract::SwmrReader<&sr_test::f_no_noexcept_reader>);
}

void test_positive_pointer_return() {
    static_assert( extract::SwmrReader<&sr_test::f_pointer_return>);
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<&sr_test::f_pointer_return>,
        int*>);
    static_assert(std::is_same_v<
        extract::swmr_reader_returned_value_t<&sr_test::f_pointer_return>,
        int*>);
}

void test_positive_struct_return() {
    static_assert( extract::SwmrReader<&sr_test::f_struct_return>);
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<&sr_test::f_struct_return>,
        sr_test::payload_struct>);
}

void test_positive_volatile_handle_admitted() {
    static_assert( extract::SwmrReader<&sr_test::f_handle_volatile>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::SwmrReader<&sr_test::f_no_param_no_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_no_param_int_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_two_params>);
}

void test_negative_handle_not_rvalue_ref() {
    static_assert(!extract::SwmrReader<&sr_test::f_handle_lvalue_ref>);
}

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::SwmrReader<&sr_test::f_handle_const_rvalue_ref>);
}

void test_negative_void_return() {
    static_assert(!extract::SwmrReader<&sr_test::f_void_return>);
}

void test_negative_reference_return() {
    // §3.6 explicitly returns by-value to avoid torn-state risk.
    static_assert(!extract::SwmrReader<&sr_test::f_lvalue_ref_return>);
    static_assert(!extract::SwmrReader<&sr_test::f_rvalue_ref_return>);
}

void test_negative_owned_region_return() {
    static_assert(!extract::SwmrReader<&sr_test::f_region_return>);
}

void test_negative_handle_slot_not_handle() {
    static_assert(!extract::SwmrReader<&sr_test::f_int_in_handle_slot>);
}

void test_negative_writer_in_reader_slot() {
    static_assert(!extract::SwmrReader<&sr_test::f_writer_in_reader_slot>);
}

void test_negative_hybrid_in_reader_slot() {
    static_assert(!extract::SwmrReader<&sr_test::f_hybrid_in_reader_slot>);
}

void test_negative_non_const_load() {
    static_assert(!extract::SwmrReader<
        &sr_test::f_non_const_load_in_reader_slot>);
}

void test_negative_void_load() {
    static_assert(!extract::SwmrReader<
        &sr_test::f_void_load_in_reader_slot>);
}

void test_positive_handle_with_extras_admitted() {
    // D07's structural detection only checks publish/load.  Extra
    // unrelated methods don't break recognition — the trait is
    // duck-typed, not nominal.
    static_assert( extract::SwmrReader<&sr_test::f_reader_with_extras>);
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<
            &sr_test::f_reader_with_extras>,
        int>);
}

void test_optional_returning_load_admitted() {
    // CURRENT-BEHAVIOUR test (NOT a desired-state test):
    // D07 admits readers with `optional<P> load() const` because
    // load_signature_decomp's P deduces to optional<int>, which is
    // non-void.  The IsSwmrHandle.h header comment claims optional
    // returns "would signal a non-blocking try-load... a different
    // shape" — but that intent is NOT enforced.
    //
    // D18 inherits the acceptance.  This test pins the current
    // state so any future tightening of D07's signature_decomp
    // (e.g., excluding optional<T> payloads) lights up this red.
    static_assert( extract::SwmrReader<&sr_test::f_optional_reader>);
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<&sr_test::f_optional_reader>,
        std::optional<int>>);
    static_assert(std::is_same_v<
        extract::swmr_reader_returned_value_t<&sr_test::f_optional_reader>,
        std::optional<int>>);
    static_assert(extract::swmr_reader_value_consistent_v<
        &sr_test::f_optional_reader>);
}

void test_unary_transform_witness_not_swmr_reader() {
    // Confirms the cross-shape exclusion in the OPPOSITE direction
    // from test_cross_shape_exclusion_full_matrix: a known
    // UnaryTransform must NOT be a SwmrReader.  IsSwmrReader
    // requires param 0 to be SWMR-reader-shaped (has const-qualified
    // load + no publish); OwnedRegion is not SWMR-reader-shaped, so
    // the exclusion is structurally enforced.
    static_assert( extract::UnaryTransform<&sr_test::f_unary_transform_witness>);
    static_assert(!extract::SwmrReader<&sr_test::f_unary_transform_witness>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<&sr_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::swmr_reader_handle_value_t<&sr_test::f_well_formed_double>,
        double>);
}

void test_returned_value_extraction() {
    static_assert(std::is_same_v<
        extract::swmr_reader_returned_value_t<&sr_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::swmr_reader_returned_value_t<&sr_test::f_value_mismatch>,
        double>);
}

void test_value_consistency_predicate() {
    static_assert(extract::swmr_reader_value_consistent_v<
        &sr_test::f_well_formed>);
    static_assert(extract::swmr_reader_value_consistent_v<
        &sr_test::f_well_formed_double>);
    static_assert(!extract::swmr_reader_value_consistent_v<
        &sr_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    auto callable_with_swmr = []<auto FnPtr>()
        requires extract::SwmrReader<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_swmr.template operator()<
        &sr_test::f_well_formed>());
    EXPECT_TRUE(callable_with_swmr.template operator()<
        &sr_test::f_well_formed_double>());
}

void test_cross_shape_exclusion_full_matrix() {
    // SwmrReader's arity 1 + non-void return overlaps UnaryTransform's
    // out-of-place case syntactically.  The per-clause exclusion is:
    // UnaryTransform requires param 0 to be OwnedRegion&&; SwmrReader
    // has a SWMR handle there, which fails OwnedRegion's tag-template
    // specialization.

    static_assert( extract::SwmrReader<&sr_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&sr_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&sr_test::f_well_formed>);
    static_assert(!extract::ProducerEndpoint<&sr_test::f_well_formed>);
    static_assert(!extract::ConsumerEndpoint<&sr_test::f_well_formed>);
    static_assert(!extract::SwmrWriter<&sr_test::f_well_formed>);

    // Region-return signature is rejected by SwmrReader (explicitly).
    static_assert(!extract::SwmrReader<&sr_test::f_region_return>);
}

void test_inferred_tags_does_not_harvest_handle_tag() {
    // D11 returns the empty set — no OwnedRegion in the signature.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&sr_test::f_well_formed>,
        Expected>);
    static_assert(extract::inferred_permission_tags_count_v<
        &sr_test::f_well_formed> == 0);
    static_assert(extract::is_tag_free_function_v<
        &sr_test::f_well_formed>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos =
        extract::is_swmr_reader_function_v<&sr_test::f_well_formed>;
    bool baseline_neg =
        !extract::is_swmr_reader_function_v<&sr_test::f_no_param_no_return>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_swmr_reader_function_v<&sr_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_swmr_reader_function_v<
                &sr_test::f_no_param_no_return>);
        EXPECT_TRUE(extract::SwmrReader<&sr_test::f_well_formed>);
        EXPECT_TRUE(!extract::SwmrReader<&sr_test::f_no_param_no_return>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_swmr_reader:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_well_formed",
             test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted",
             test_positive_value_mismatch_admitted);
    run_test("test_positive_non_noexcept_load",
             test_positive_non_noexcept_load);
    run_test("test_positive_pointer_return",
             test_positive_pointer_return);
    run_test("test_positive_struct_return",
             test_positive_struct_return);
    run_test("test_positive_volatile_handle_admitted",
             test_positive_volatile_handle_admitted);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref",
             test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref",
             test_negative_handle_const_rvalue_ref);
    run_test("test_negative_void_return",
             test_negative_void_return);
    run_test("test_negative_reference_return",
             test_negative_reference_return);
    run_test("test_negative_owned_region_return",
             test_negative_owned_region_return);
    run_test("test_negative_handle_slot_not_handle",
             test_negative_handle_slot_not_handle);
    run_test("test_negative_writer_in_reader_slot",
             test_negative_writer_in_reader_slot);
    run_test("test_negative_hybrid_in_reader_slot",
             test_negative_hybrid_in_reader_slot);
    run_test("test_negative_non_const_load",
             test_negative_non_const_load);
    run_test("test_negative_void_load",
             test_negative_void_load);
    run_test("test_positive_handle_with_extras_admitted",
             test_positive_handle_with_extras_admitted);
    run_test("test_optional_returning_load_admitted",
             test_optional_returning_load_admitted);
    run_test("test_unary_transform_witness_not_swmr_reader",
             test_unary_transform_witness_not_swmr_reader);
    run_test("test_handle_value_extraction",
             test_handle_value_extraction);
    run_test("test_returned_value_extraction",
             test_returned_value_extraction);
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
