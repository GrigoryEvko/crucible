// ═══════════════════════════════════════════════════════════════════
// test_is_budgeted — sentinel TU for safety/IsBudgeted.h
//
// FOUND-D30 (fifth wrapper of batch — FIRST product wrapper).
//
// Shape divergence: Budgeted is `template<typename T>` only — its two
// budget axes are RUNTIME values, not compile-time NTTPs.  Test
// surface differs from the prior 4 D30 detectors (CipherTier /
// ResidencyHeat / Vendor / Crash):
//
//   - NO tag-extraction tests (no compile-time tag exists).
//   - Layout invariant is `>= sizeof(T) + 16` (runtime grade pair).
//   - Public-alias tests are over `BudgetedInt` / `BudgetedDbl`
//     type-aliases (Budgeted has no `safety::budgeted::` namespace
//     of template aliases).
//   - 21 audit-extended tests (vs 25 for NTTP wrappers).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsBudgeted.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SignatureTraits.h>
#include <crucible/safety/Vendor.h>

#include <cstdint>
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

using B_int      = safety::Budgeted<int>;
using B_double   = safety::Budgeted<double>;
using B_char     = safety::Budgeted<char>;
using B_uint64   = safety::Budgeted<std::uint64_t>;
using B_float    = safety::Budgeted<float>;

struct test_tag {};
using OR_int_test     = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using Cn_int_strong   = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using DS_int_pure     = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using CT_int_hot      = safety::CipherTier<extract::CipherTierTag_v::Hot, int>;
using RH_int_hot      = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, int>;
using V_int_nv        = safety::Vendor<extract::VendorBackend_v::NV, int>;
using C_int_no_throw  = safety::Crash<extract::CrashClass_v::NoThrow, int>;

struct payload_struct { int a; double b; };
using B_struct = safety::Budgeted<payload_struct>;
using B_nested = safety::Budgeted<B_int>;

}  // namespace

namespace b_test {
void f_takes_b_int(B_int const&) noexcept;
void f_takes_b_double(B_double&&) noexcept;
B_uint64 f_returns_b_uint64(int) noexcept;
}  // namespace b_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_budgeted_smoke_test());
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_budgeted_v<B_int>);
    static_assert( extract::is_budgeted_v<B_double>);
    static_assert( extract::is_budgeted_v<B_char>);
    static_assert( extract::is_budgeted_v<B_uint64>);
    static_assert( extract::is_budgeted_v<B_float>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_budgeted_v<B_int>);
    static_assert( extract::is_budgeted_v<B_int&>);
    static_assert( extract::is_budgeted_v<B_int&&>);
    static_assert( extract::is_budgeted_v<B_int const&>);
    static_assert( extract::is_budgeted_v<B_int const>);
    static_assert( extract::is_budgeted_v<B_int volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_budgeted_v<int>);
    static_assert(!extract::is_budgeted_v<double>);
    static_assert(!extract::is_budgeted_v<void>);
    static_assert(!extract::is_budgeted_v<safety::BitsBudget>);
    static_assert(!extract::is_budgeted_v<safety::PeakBytes>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_budgeted_v<int*>);
    static_assert(!extract::is_budgeted_v<B_int*>);
    static_assert(!extract::is_budgeted_v<B_int[5]>);
    static_assert(!extract::is_budgeted_v<B_int* const>);
}

void test_negative_lookalike_struct() {
    // A POD that mimics Budgeted's storage shape (value + two
    // uint64_t budget fields).  Wrapper-detector keys on the WRAPPER
    // CLASS, not the storage layout.
    struct LookalikeBudgeted {
        int value;
        std::uint64_t bits_field;
        std::uint64_t peak_field;
    };
    static_assert(!extract::is_budgeted_v<LookalikeBudgeted>);
}

void test_concept_form_in_constraints() {
    auto callable_with_b = []<typename T>()
        requires extract::IsBudgeted<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_b.template operator()<B_int>());
    EXPECT_TRUE(callable_with_b.template operator()<B_double>());
    EXPECT_TRUE(callable_with_b.template operator()<B_uint64>());
    EXPECT_TRUE(callable_with_b.template operator()<B_struct>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::budgeted_value_t<B_int>,    int>);
    static_assert(std::is_same_v<extract::budgeted_value_t<B_double>, double>);
    static_assert(std::is_same_v<extract::budgeted_value_t<B_char>,   char>);
    static_assert(std::is_same_v<extract::budgeted_value_t<B_uint64>, std::uint64_t>);
    static_assert(std::is_same_v<extract::budgeted_value_t<B_float>,  float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::budgeted_value_t<B_int const&>, int>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<B_int&&>, int>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<B_double const>, double>);
}

void test_distinct_element_types_distinct_specs() {
    static_assert(!std::is_same_v<B_int, B_double>);
    static_assert(!std::is_same_v<
        extract::budgeted_value_t<B_int>,
        extract::budgeted_value_t<B_double>>);
    static_assert(!std::is_same_v<B_int, B_uint64>);

    // Same wrapped element type → same Budgeted instantiation
    using B_int_alt = safety::Budgeted<int>;
    static_assert(std::is_same_v<B_int, B_int_alt>);
}

void test_runtime_grade_independence_from_type_identity() {
    // Two Budgeted<int> values with DIFFERENT runtime budgets are
    // STILL the same C++ type — the wrapper's identity is type-level
    // and unaffected by runtime grade.  Pin this so a future refactor
    // that promotes the budget axes to NTTPs (which would break the
    // wrapper's measure-at-construction-site purpose) breaks the
    // build loudly here.
    B_int b_zero{0,    safety::BitsBudget{0},    safety::PeakBytes{0}};
    B_int b_huge{0,    safety::BitsBudget{1<<20}, safety::PeakBytes{1<<30}};
    static_assert(std::is_same_v<decltype(b_zero), decltype(b_huge)>);

    // Detector returns true for both; no runtime-grade dependency.
    EXPECT_TRUE(extract::is_budgeted_v<decltype(b_zero)>);
    EXPECT_TRUE(extract::is_budgeted_v<decltype(b_huge)>);
}

void test_dispatcher_value_type_use_case() {
    // Dispatcher pattern: branch on the WRAPPED element type after
    // confirming Budgeted-ness.  Mirrors a Forge phase that lowers
    // budgeted scalars vs budgeted aggregates differently.
    auto select_lowering = []<typename T>() consteval
        requires extract::IsBudgeted<T>
    {
        using V = extract::budgeted_value_t<T>;
        if constexpr (std::is_arithmetic_v<V>) return 1;
        else if constexpr (std::is_class_v<V>) return 2;
        else                                    return 0;
    };

    static_assert(select_lowering.template operator()<B_int>()    == 1);
    static_assert(select_lowering.template operator()<B_double>() == 1);
    static_assert(select_lowering.template operator()<B_struct>() == 2);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_budgeted_v<B_struct>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<B_struct>, payload_struct>);
}

void test_positive_nested_budgeted() {
    static_assert( extract::is_budgeted_v<B_nested>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<B_nested>, B_int>);
    static_assert( extract::is_budgeted_v<extract::budgeted_value_t<B_nested>>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<extract::budgeted_value_t<B_nested>>, int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&b_test::f_takes_b_int, 0>;
    static_assert( ex::is_budgeted_v<P0>);
    static_assert(std::is_same_v<ex::budgeted_value_t<P0>, int>);

    using P0_double = ex::param_type_t<&b_test::f_takes_b_double, 0>;
    static_assert( ex::is_budgeted_v<P0_double>);
    static_assert(std::is_same_v<ex::budgeted_value_t<P0_double>, double>);

    using R = ex::return_type_t<&b_test::f_returns_b_uint64>;
    static_assert( ex::is_budgeted_v<R>);
    static_assert(std::is_same_v<ex::budgeted_value_t<R>, std::uint64_t>);
}

void test_local_alias_round_trip() {
    using LocalBInt = safety::Budgeted<int>;
    static_assert( extract::is_budgeted_v<LocalBInt>);
    static_assert(std::is_same_v<LocalBInt, B_int>);
    static_assert(std::is_same_v<extract::budgeted_value_t<LocalBInt>, int>);
}

void test_runtime_grade_layout_invariant() {
    // FIRST product wrapper in D30 batch — divergent layout assertion.
    // Budgeted carries a runtime ProductLattice<BitsBudget, PeakBytes>
    // grade pair (16 bytes for two uint64_t); sizeof is therefore
    // STRICTLY GREATER than sizeof(T) for non-trivially-aligned T,
    // and EXACTLY sizeof(T) + 16 for uint64_t (alignment-matched).
    static_assert(sizeof(B_int)    >= sizeof(int)    + 16);
    static_assert(sizeof(B_double) >= sizeof(double) + 16);
    static_assert(sizeof(B_char)   >= sizeof(char)   + 16);
    static_assert(sizeof(B_uint64) == 24);
    static_assert(sizeof(B_struct) >= sizeof(payload_struct) + 16);

    // Pin the divergence from NTTP wrappers: NTTP wrappers achieve
    // sizeof == sizeof(T) via empty-base optimization on the static
    // grade.  Budgeted CANNOT — its grade is per-instance data.
    static_assert(sizeof(B_int)  > sizeof(int));
    static_assert(sizeof(V_int_nv) == sizeof(int));  // contrast: NTTP wrapper
}

void test_public_aliases_round_trip() {
    // Budgeted exposes type aliases (already-instantiated), NOT
    // template aliases like vendor::Nv<T>.  Test surface differs.
    using BudgetedInt    = safety::Budgeted<int>;
    using BudgetedDouble = safety::Budgeted<double>;

    static_assert( extract::is_budgeted_v<BudgetedInt>);
    static_assert( extract::is_budgeted_v<BudgetedDouble>);
    static_assert(std::is_same_v<BudgetedInt,    B_int>);
    static_assert(std::is_same_v<BudgetedDouble, B_double>);

    static_assert(std::is_same_v<
        extract::budgeted_value_t<BudgetedInt>, int>);
    static_assert(std::is_same_v<
        extract::budgeted_value_t<BudgetedDouble>, double>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<B_int>);
    static_assert( extract::is_graded_wrapper_v<B_double>);
    static_assert( extract::is_graded_wrapper_v<B_uint64>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<B_int>,
        extract::budgeted_value_t<B_int>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_budgeted_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfB = safety::Consistency<
        extract::Consistency_v::STRONG, B_int>;
    using BOfConsistency = safety::Budgeted<Cn_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfB>);
    static_assert(!extract::is_budgeted_v<ConsistencyOfB>);
    static_assert( extract::is_budgeted_v<
        extract::consistency_value_t<ConsistencyOfB>>);

    static_assert( extract::is_budgeted_v<BOfConsistency>);
    static_assert(!extract::is_consistency_v<BOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::budgeted_value_t<BOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // Budgeted vs all 9 other shipped wrappers — fully disjoint.
    static_assert( extract::is_budgeted_v<B_int>);
    static_assert(!extract::is_owned_region_v<B_int>);
    static_assert(!extract::is_numerical_tier_v<B_int>);
    static_assert(!extract::is_consistency_v<B_int>);
    static_assert(!extract::is_opaque_lifetime_v<B_int>);
    static_assert(!extract::is_det_safe_v<B_int>);
    static_assert(!extract::is_cipher_tier_v<B_int>);
    static_assert(!extract::is_residency_heat_v<B_int>);
    static_assert(!extract::is_vendor_v<B_int>);
    static_assert(!extract::is_crash_v<B_int>);

    static_assert(!extract::is_budgeted_v<OR_int_test>);
    static_assert(!extract::is_budgeted_v<NT_int_bitexact>);
    static_assert(!extract::is_budgeted_v<Cn_int_strong>);
    static_assert(!extract::is_budgeted_v<OL_int_fleet>);
    static_assert(!extract::is_budgeted_v<DS_int_pure>);
    static_assert(!extract::is_budgeted_v<CT_int_hot>);
    static_assert(!extract::is_budgeted_v<RH_int_hot>);
    static_assert(!extract::is_budgeted_v<V_int_nv>);
    static_assert(!extract::is_budgeted_v<C_int_no_throw>);

    static_assert(!extract::is_budgeted_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_budgeted_v<B_int>;
    bool baseline_neg = !extract::is_budgeted_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_budgeted_v<B_int>);
        EXPECT_TRUE(baseline_neg == !extract::is_budgeted_v<int>);
        EXPECT_TRUE(extract::IsBudgeted<B_double&&>);
        EXPECT_TRUE(!extract::IsBudgeted<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_budgeted:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
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
    run_test("test_distinct_element_types_distinct_specs",
             test_distinct_element_types_distinct_specs);
    run_test("test_runtime_grade_independence_from_type_identity",
             test_runtime_grade_independence_from_type_identity);
    run_test("test_dispatcher_value_type_use_case",
             test_dispatcher_value_type_use_case);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_budgeted",
             test_positive_nested_budgeted);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip", test_local_alias_round_trip);
    run_test("test_runtime_grade_layout_invariant",
             test_runtime_grade_layout_invariant);
    run_test("test_public_aliases_round_trip",
             test_public_aliases_round_trip);
    run_test("test_graded_wrapper_conformance",
             test_graded_wrapper_conformance);
    run_test("test_outer_wrapper_identity_dominates_nested",
             test_outer_wrapper_identity_dominates_nested);
    run_test("test_cross_wrapper_exclusion", test_cross_wrapper_exclusion);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
