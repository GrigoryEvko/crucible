// ═══════════════════════════════════════════════════════════════════
// test_is_numerical_tier — sentinel TU for safety/IsNumericalTier.h
//
// FOUND-D21: first wrapper-detection extractor for the G-series
// product wrappers.  Mechanical extension of D03's pattern
// (test_is_owned_region) — partial-specialization-detects-NTTP.
//
// Coverage:
//   * Positive: wrapped int / double / float, every Tolerance tier.
//   * Cv-ref stripping on every reference category.
//   * Negative: bare types, pointers, references-to-pointers, void,
//     lookalike struct (same shape, wrong type).
//   * Concept form composes in `requires`-clauses.
//   * Element type extraction across distinct (Tier, T) instantiations.
//   * Tier extraction — every Tolerance enum value round-trips.
//   * Distinct tiers, same element type, distinct trait specs.
//   * Same tier, distinct element types, distinct trait specs.
//   * Tier-aware dispatch worked example (compile-time routing).
//   * Cross-wrapper exclusion: NumericalTier vs OwnedRegion vs
//     Permission — all wrapper detectors agree these are disjoint.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsNumericalTier.h>

#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/SignatureTraits.h>

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

// All seven Tolerance tiers as worked examples.
using NT_int_bitexact     = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using NT_int_ulp_fp64     = safety::NumericalTier<extract::Tolerance::ULP_FP64, int>;
using NT_int_ulp_fp32     = safety::NumericalTier<extract::Tolerance::ULP_FP32, int>;
using NT_int_ulp_fp16     = safety::NumericalTier<extract::Tolerance::ULP_FP16, int>;
using NT_int_ulp_fp8      = safety::NumericalTier<extract::Tolerance::ULP_FP8,  int>;
using NT_int_ulp_int8     = safety::NumericalTier<extract::Tolerance::ULP_INT8, int>;
using NT_int_relaxed      = safety::NumericalTier<extract::Tolerance::RELAXED,  int>;

// Different element types under one tier.
using NT_double_bitexact  = safety::NumericalTier<extract::Tolerance::BITEXACT, double>;
using NT_float_bitexact   = safety::NumericalTier<extract::Tolerance::BITEXACT, float>;

// Cross-wrapper exclusion witnesses.
struct test_tag {};
using OR_int_test = safety::OwnedRegion<int, test_tag>;

// Non-fundamental wrapped type (struct with members).
struct payload_struct { double a; int b; };
using NT_struct_bitexact = safety::NumericalTier<
    extract::Tolerance::BITEXACT, payload_struct>;

// Nested NumericalTier — outer wrapper around an inner-wrapped value.
using NT_nested = safety::NumericalTier<
    extract::Tolerance::BITEXACT, NT_int_bitexact>;

}  // namespace

// Function declarations exercising the dispatcher use case live in
// a NAMED namespace because anonymous-namespace functions referenced
// via `&fn` in static_asserts trigger -Werror=unused-function (the
// linker-internal name is unique per TU but the analyzer doesn't
// treat the static_assert reference as a use).
namespace nt_test {
void f_takes_bitexact_int(NT_int_bitexact const&) noexcept;
void f_takes_relaxed_int(NT_int_relaxed&&) noexcept;
NT_double_bitexact f_returns_bitexact_double(int) noexcept;
}  // namespace nt_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_numerical_tier_smoke_test());
}

void test_positive_every_tier() {
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact>);
    static_assert( extract::is_numerical_tier_v<NT_int_ulp_fp64>);
    static_assert( extract::is_numerical_tier_v<NT_int_ulp_fp32>);
    static_assert( extract::is_numerical_tier_v<NT_int_ulp_fp16>);
    static_assert( extract::is_numerical_tier_v<NT_int_ulp_fp8>);
    static_assert( extract::is_numerical_tier_v<NT_int_ulp_int8>);
    static_assert( extract::is_numerical_tier_v<NT_int_relaxed>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_numerical_tier_v<NT_double_bitexact>);
    static_assert( extract::is_numerical_tier_v<NT_float_bitexact>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact&>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact&&>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact const&>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact const>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_numerical_tier_v<int>);
    static_assert(!extract::is_numerical_tier_v<double>);
    static_assert(!extract::is_numerical_tier_v<void>);
    static_assert(!extract::is_numerical_tier_v<extract::Tolerance>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_numerical_tier_v<int*>);
    static_assert(!extract::is_numerical_tier_v<int**>);
    static_assert(!extract::is_numerical_tier_v<NT_int_bitexact*>);
    static_assert(!extract::is_numerical_tier_v<NT_int_bitexact[5]>);
    // Pointer-to-NumericalTier is NOT a NumericalTier.
    static_assert(!extract::is_numerical_tier_v<NT_int_bitexact* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeNumericalTier { int value; extract::Tolerance tier; };
    static_assert(!extract::is_numerical_tier_v<LookalikeNumericalTier>);
}

void test_concept_form_in_constraints() {
    auto callable_with_nt = []<typename T>()
        requires extract::IsNumericalTier<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_nt.template operator()<NT_int_bitexact>());
    EXPECT_TRUE(callable_with_nt.template operator()<NT_int_relaxed>());
    EXPECT_TRUE(callable_with_nt.template operator()<NT_double_bitexact>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_int_bitexact>, int>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_int_relaxed>, int>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_double_bitexact>, double>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_float_bitexact>, float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_int_bitexact const&>, int>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_int_bitexact&&>, int>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_double_bitexact const>, double>);
}

void test_tier_extraction_every_value() {
    // Every Tolerance enum value must round-trip through the
    // wrapper-detection trait.
    static_assert(extract::numerical_tier_v<NT_int_bitexact>
                  == extract::Tolerance::BITEXACT);
    static_assert(extract::numerical_tier_v<NT_int_ulp_fp64>
                  == extract::Tolerance::ULP_FP64);
    static_assert(extract::numerical_tier_v<NT_int_ulp_fp32>
                  == extract::Tolerance::ULP_FP32);
    static_assert(extract::numerical_tier_v<NT_int_ulp_fp16>
                  == extract::Tolerance::ULP_FP16);
    static_assert(extract::numerical_tier_v<NT_int_ulp_fp8>
                  == extract::Tolerance::ULP_FP8);
    static_assert(extract::numerical_tier_v<NT_int_ulp_int8>
                  == extract::Tolerance::ULP_INT8);
    static_assert(extract::numerical_tier_v<NT_int_relaxed>
                  == extract::Tolerance::RELAXED);
}

void test_tier_extraction_cv_ref_strips() {
    static_assert(extract::numerical_tier_v<NT_int_bitexact const&>
                  == extract::Tolerance::BITEXACT);
    static_assert(extract::numerical_tier_v<NT_int_relaxed&&>
                  == extract::Tolerance::RELAXED);
}

void test_distinct_tiers_distinct_specs() {
    // Same element type, different tiers — distinct trait specs.
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_int_bitexact>,
        extract::numerical_tier_value_t<NT_int_relaxed>>);  // both int
    static_assert(extract::numerical_tier_v<NT_int_bitexact>
                  != extract::numerical_tier_v<NT_int_relaxed>);

    // Same tier, different element types — value types differ.
    static_assert(extract::numerical_tier_v<NT_int_bitexact>
                  == extract::numerical_tier_v<NT_double_bitexact>);
    static_assert(!std::is_same_v<
        extract::numerical_tier_value_t<NT_int_bitexact>,
        extract::numerical_tier_value_t<NT_double_bitexact>>);
}

void test_dispatcher_integration_example() {
    // Worked example: tier-aware dispatch.  A dispatcher seeing a
    // NumericalTier-wrapped tensor parameter can route at compile
    // time on the pinned tier without runtime branches.
    auto select_lowering = []<typename T>() consteval
        requires extract::IsNumericalTier<T>
    {
        constexpr auto tier = extract::numerical_tier_v<T>;
        if constexpr (tier == extract::Tolerance::BITEXACT) {
            return 7;  // strictest; no reordering
        } else if constexpr (tier == extract::Tolerance::ULP_FP64) {
            return 6;
        } else if constexpr (tier == extract::Tolerance::ULP_FP32) {
            return 5;
        } else if constexpr (tier == extract::Tolerance::ULP_FP16) {
            return 4;
        } else if constexpr (tier == extract::Tolerance::ULP_FP8) {
            return 3;
        } else if constexpr (tier == extract::Tolerance::ULP_INT8) {
            return 2;
        } else {
            return 1;  // RELAXED — most permissive
        }
    };

    static_assert(select_lowering.template operator()<NT_int_bitexact>() == 7);
    static_assert(select_lowering.template operator()<NT_int_ulp_fp64>() == 6);
    static_assert(select_lowering.template operator()<NT_int_ulp_fp32>() == 5);
    static_assert(select_lowering.template operator()<NT_int_ulp_fp16>() == 4);
    static_assert(select_lowering.template operator()<NT_int_ulp_fp8>()  == 3);
    static_assert(select_lowering.template operator()<NT_int_ulp_int8>() == 2);
    static_assert(select_lowering.template operator()<NT_int_relaxed>()  == 1);
}

void test_positive_non_fundamental_wrapped_type() {
    // Wrapping a struct works identically to wrapping a fundamental.
    static_assert( extract::is_numerical_tier_v<NT_struct_bitexact>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_struct_bitexact>,
        payload_struct>);
    static_assert(extract::numerical_tier_v<NT_struct_bitexact>
                  == extract::Tolerance::BITEXACT);
}

void test_positive_nested_numerical_tier() {
    // Nested NumericalTier — outer wrapper around an inner-wrapped
    // value.  The OUTER predicate matches; the value_type extraction
    // recovers the inner-wrapped TYPE (which is itself a
    // NumericalTier specialization).
    static_assert( extract::is_numerical_tier_v<NT_nested>);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<NT_nested>, NT_int_bitexact>);
    // The inner value type is itself a NumericalTier — recursive
    // detection works.
    static_assert( extract::is_numerical_tier_v<
        extract::numerical_tier_value_t<NT_nested>>);
    // Recursing one more level recovers the bare int.
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<
            extract::numerical_tier_value_t<NT_nested>>,
        int>);
}

void test_dispatcher_function_parameter_use_case() {
    // The actual dispatcher use case — param_type_t flows through
    // the wrapper-detection trait.  This verifies the trait works
    // end-to-end with the FOUND-D02 splice helper.

    namespace ex = ::crucible::safety::extract;

    // f_takes_bitexact_int takes NT_int_bitexact const& as param 0.
    using P0 = ex::param_type_t<&nt_test::f_takes_bitexact_int, 0>;
    static_assert( ex::is_numerical_tier_v<P0>);
    // Cv-ref strip in the trait recovers the underlying tier.
    static_assert(ex::numerical_tier_v<P0> == ex::Tolerance::BITEXACT);
    static_assert(std::is_same_v<ex::numerical_tier_value_t<P0>, int>);

    // f_takes_relaxed_int takes NT_int_relaxed&& as param 0.
    using P0_relaxed = ex::param_type_t<&nt_test::f_takes_relaxed_int, 0>;
    static_assert( ex::is_numerical_tier_v<P0_relaxed>);
    static_assert(ex::numerical_tier_v<P0_relaxed>
                  == ex::Tolerance::RELAXED);

    // Return type also flows through the trait.
    using R = ex::return_type_t<&nt_test::f_returns_bitexact_double>;
    static_assert( ex::is_numerical_tier_v<R>);
    static_assert(std::is_same_v<ex::numerical_tier_value_t<R>, double>);
    static_assert(ex::numerical_tier_v<R> == ex::Tolerance::BITEXACT);
}

void test_local_alias_round_trip() {
    // User-defined aliases over NumericalTier — common pattern in
    // production where a project pins commonly-used tier+type pairs
    // to short names.  The trait must look through any user-defined
    // aliasing.
    using LocalBitexactInt = safety::NumericalTier<
        extract::Tolerance::BITEXACT, int>;
    using LocalFp32Double  = safety::NumericalTier<
        extract::Tolerance::ULP_FP32, double>;

    static_assert( extract::is_numerical_tier_v<LocalBitexactInt>);
    static_assert( extract::is_numerical_tier_v<LocalFp32Double>);
    static_assert(extract::numerical_tier_v<LocalBitexactInt>
                  == extract::Tolerance::BITEXACT);
    static_assert(extract::numerical_tier_v<LocalFp32Double>
                  == extract::Tolerance::ULP_FP32);
    static_assert(std::is_same_v<
        extract::numerical_tier_value_t<LocalBitexactInt>, int>);
}

void test_cross_wrapper_exclusion() {
    // Each wrapper-detection predicate distinguishes its OWN
    // wrapper from the others.  NumericalTier is NOT OwnedRegion,
    // is NOT Permission — and vice versa.

    // NumericalTier admitted by its own predicate.
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact>);
    // NOT by other wrapper detectors.
    static_assert(!extract::is_owned_region_v<NT_int_bitexact>);
    static_assert(!extract::is_permission_v<NT_int_bitexact>);

    // OwnedRegion NOT admitted by NumericalTier predicate.
    static_assert(!extract::is_numerical_tier_v<OR_int_test>);
    static_assert( extract::is_owned_region_v<OR_int_test>);

    // Bare int admitted by NONE.
    static_assert(!extract::is_numerical_tier_v<int>);
    static_assert(!extract::is_owned_region_v<int>);
    static_assert(!extract::is_permission_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_numerical_tier_v<NT_int_bitexact>;
    bool baseline_neg = !extract::is_numerical_tier_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_numerical_tier_v<NT_int_bitexact>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_numerical_tier_v<int>);
        EXPECT_TRUE(extract::IsNumericalTier<NT_int_bitexact&&>);
        EXPECT_TRUE(!extract::IsNumericalTier<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_numerical_tier:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_every_tier", test_positive_every_tier);
    run_test("test_positive_distinct_element_types",
             test_positive_distinct_element_types);
    run_test("test_cv_ref_stripping", test_cv_ref_stripping);
    run_test("test_negative_bare_types", test_negative_bare_types);
    run_test("test_negative_pointers_and_arrays",
             test_negative_pointers_and_arrays);
    run_test("test_negative_lookalike_struct",
             test_negative_lookalike_struct);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_value_type_extraction", test_value_type_extraction);
    run_test("test_value_type_extraction_cv_ref_strips",
             test_value_type_extraction_cv_ref_strips);
    run_test("test_tier_extraction_every_value",
             test_tier_extraction_every_value);
    run_test("test_tier_extraction_cv_ref_strips",
             test_tier_extraction_cv_ref_strips);
    run_test("test_distinct_tiers_distinct_specs",
             test_distinct_tiers_distinct_specs);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_numerical_tier",
             test_positive_nested_numerical_tier);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip",
             test_local_alias_round_trip);
    run_test("test_cross_wrapper_exclusion",
             test_cross_wrapper_exclusion);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
