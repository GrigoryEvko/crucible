// ═══════════════════════════════════════════════════════════════════
// test_is_recipe_spec — sentinel TU for safety/IsRecipeSpec.h
//
// FOUND-D30 (eighth and FINAL wrapper of batch — fourth product
// wrapper).  Closes out the D30 batch.
//
// Distinguishing axis: RecipeSpec is the SMALLEST product wrapper
// in the D30 batch (2-byte grade overhead vs 16 for Budgeted/
// EpochVersioned and 40 for NumaPlacement).  The cross-product-
// wrapper layout disagreement test pins all three sizes
// distinctly so the dispatcher's "iterate over a tuple of product
// wrappers" pattern handles each correctly.
//
// Additional distinguishing test: shared first axis with
// NumericalTier (both Tolerance-keyed) — RS_int and NT_int_bitexact
// MUST be distinct types despite the shared sub-lattice.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsRecipeSpec.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/RecipeSpec.h>
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

using RS_int      = safety::RecipeSpec<int>;
using RS_double   = safety::RecipeSpec<double>;
using RS_char     = safety::RecipeSpec<char>;
using RS_uint64   = safety::RecipeSpec<std::uint64_t>;
using RS_float    = safety::RecipeSpec<float>;

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
using B_int           = safety::Budgeted<int>;
using EV_int          = safety::EpochVersioned<int>;
using NP_int          = safety::NumaPlacement<int>;

struct payload_struct { int a; double b; };
using RS_struct = safety::RecipeSpec<payload_struct>;
using RS_nested = safety::RecipeSpec<RS_int>;

}  // namespace

namespace rs_test {
void f_takes_rs_int(RS_int const&) noexcept;
void f_takes_rs_double(RS_double&&) noexcept;
RS_uint64 f_returns_rs_uint64(int) noexcept;
}  // namespace rs_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_recipe_spec_smoke_test());
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_recipe_spec_v<RS_int>);
    static_assert( extract::is_recipe_spec_v<RS_double>);
    static_assert( extract::is_recipe_spec_v<RS_char>);
    static_assert( extract::is_recipe_spec_v<RS_uint64>);
    static_assert( extract::is_recipe_spec_v<RS_float>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_recipe_spec_v<RS_int>);
    static_assert( extract::is_recipe_spec_v<RS_int&>);
    static_assert( extract::is_recipe_spec_v<RS_int&&>);
    static_assert( extract::is_recipe_spec_v<RS_int const&>);
    static_assert( extract::is_recipe_spec_v<RS_int const>);
    static_assert( extract::is_recipe_spec_v<RS_int volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_recipe_spec_v<int>);
    static_assert(!extract::is_recipe_spec_v<double>);
    static_assert(!extract::is_recipe_spec_v<void>);
    static_assert(!extract::is_recipe_spec_v<safety::Tolerance>);
    static_assert(!extract::is_recipe_spec_v<safety::RecipeFamily>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_recipe_spec_v<int*>);
    static_assert(!extract::is_recipe_spec_v<RS_int*>);
    static_assert(!extract::is_recipe_spec_v<RS_int[5]>);
    static_assert(!extract::is_recipe_spec_v<RS_int* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeRecipeSpec {
        int value;
        safety::Tolerance tol_field;
        safety::RecipeFamily fam_field;
    };
    static_assert(!extract::is_recipe_spec_v<LookalikeRecipeSpec>);
}

void test_concept_form_in_constraints() {
    auto callable_with_rs = []<typename T>()
        requires extract::IsRecipeSpec<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_rs.template operator()<RS_int>());
    EXPECT_TRUE(callable_with_rs.template operator()<RS_double>());
    EXPECT_TRUE(callable_with_rs.template operator()<RS_uint64>());
    EXPECT_TRUE(callable_with_rs.template operator()<RS_struct>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::recipe_spec_value_t<RS_int>,    int>);
    static_assert(std::is_same_v<extract::recipe_spec_value_t<RS_double>, double>);
    static_assert(std::is_same_v<extract::recipe_spec_value_t<RS_char>,   char>);
    static_assert(std::is_same_v<extract::recipe_spec_value_t<RS_uint64>, std::uint64_t>);
    static_assert(std::is_same_v<extract::recipe_spec_value_t<RS_float>,  float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RS_int const&>, int>);
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RS_int&&>, int>);
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RS_double const>, double>);
}

void test_distinct_element_types_distinct_specs() {
    static_assert(!std::is_same_v<RS_int, RS_double>);
    static_assert(!std::is_same_v<
        extract::recipe_spec_value_t<RS_int>,
        extract::recipe_spec_value_t<RS_double>>);
    using RS_int_alt = safety::RecipeSpec<int>;
    static_assert(std::is_same_v<RS_int, RS_int_alt>);
}

void test_runtime_grade_independence_from_type_identity() {
    RS_int rs_default{};
    RS_int rs_explicit{0, safety::Tolerance::BITEXACT,
                       safety::RecipeFamily::Kahan};
    static_assert(std::is_same_v<decltype(rs_default), decltype(rs_explicit)>);

    EXPECT_TRUE(extract::is_recipe_spec_v<decltype(rs_default)>);
    EXPECT_TRUE(extract::is_recipe_spec_v<decltype(rs_explicit)>);
}

void test_dispatcher_value_type_use_case() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsRecipeSpec<T>
    {
        using V = extract::recipe_spec_value_t<T>;
        if constexpr (std::is_arithmetic_v<V>) return 1;
        else if constexpr (std::is_class_v<V>) return 2;
        else                                    return 0;
    };

    static_assert(select_lowering.template operator()<RS_int>()    == 1);
    static_assert(select_lowering.template operator()<RS_double>() == 1);
    static_assert(select_lowering.template operator()<RS_struct>() == 2);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_recipe_spec_v<RS_struct>);
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RS_struct>, payload_struct>);
}

void test_positive_nested_recipe_spec() {
    static_assert( extract::is_recipe_spec_v<RS_nested>);
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RS_nested>, RS_int>);
    static_assert( extract::is_recipe_spec_v<
        extract::recipe_spec_value_t<RS_nested>>);
    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<
            extract::recipe_spec_value_t<RS_nested>>, int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&rs_test::f_takes_rs_int, 0>;
    static_assert( ex::is_recipe_spec_v<P0>);
    static_assert(std::is_same_v<ex::recipe_spec_value_t<P0>, int>);

    using P0_double = ex::param_type_t<&rs_test::f_takes_rs_double, 0>;
    static_assert( ex::is_recipe_spec_v<P0_double>);
    static_assert(std::is_same_v<ex::recipe_spec_value_t<P0_double>, double>);

    using R = ex::return_type_t<&rs_test::f_returns_rs_uint64>;
    static_assert( ex::is_recipe_spec_v<R>);
    static_assert(std::is_same_v<ex::recipe_spec_value_t<R>, std::uint64_t>);
}

void test_local_alias_round_trip() {
    using LocalRsInt = safety::RecipeSpec<int>;
    static_assert( extract::is_recipe_spec_v<LocalRsInt>);
    static_assert(std::is_same_v<LocalRsInt, RS_int>);
    static_assert(std::is_same_v<extract::recipe_spec_value_t<LocalRsInt>, int>);
}

void test_runtime_grade_layout_invariant() {
    // SMALLEST product wrapper grade — 2 bytes (1 byte Tolerance + 1
    // byte RecipeFamily, both uint8_t-backed enums, no alignment pad).
    static_assert(sizeof(RS_int)    >= sizeof(int)    + 2);
    static_assert(sizeof(RS_double) >= sizeof(double) + 2);
    static_assert(sizeof(RS_char)   >= sizeof(char)   + 2);
    static_assert(sizeof(RS_struct) >= sizeof(payload_struct) + 2);

    // Contrast: NTTP wrapper still achieves sizeof == sizeof(T).
    static_assert(sizeof(RS_int) > sizeof(int));
    static_assert(sizeof(V_int_nv) == sizeof(int));
}

void test_three_distinct_product_wrapper_layouts() {
    // FULL CROSS-PRODUCT-WRAPPER LAYOUT MAP.  All four product
    // wrappers in the D30 batch now have known grade sizes:
    //
    //     Budgeted        16 bytes (2 × uint64_t)
    //     EpochVersioned  16 bytes (2 × uint64_t)
    //     NumaPlacement   40 bytes (1 + 7 pad + 32 AffinityMask)
    //     RecipeSpec       2 bytes (2 × uint8_t)
    //
    // Three distinct sizes — no two product wrappers are
    // layout-interchangeable except (Budgeted, EpochVersioned).
    // Pin the full ordering so a future grade-axis refactor (e.g.,
    // adding a third axis to RecipeSpec) breaks the build loudly
    // here AND alerts dispatcher code that iterates over a tuple of
    // product-wrappers.
    static_assert(sizeof(RS_int) < sizeof(B_int));   // RS smallest
    static_assert(sizeof(B_int)  < sizeof(NP_int));  // NP largest
    static_assert(sizeof(B_int) == sizeof(EV_int));  // B == EV
    static_assert(sizeof(RS_int) < sizeof(EV_int));
    static_assert(sizeof(RS_int) < sizeof(NP_int));

    // All four are distinct C++ types.
    static_assert(!std::is_same_v<RS_int, B_int>);
    static_assert(!std::is_same_v<RS_int, EV_int>);
    static_assert(!std::is_same_v<RS_int, NP_int>);
    static_assert( extract::is_recipe_spec_v<RS_int>);
    static_assert(!extract::is_recipe_spec_v<B_int>);
    static_assert(!extract::is_recipe_spec_v<EV_int>);
    static_assert(!extract::is_recipe_spec_v<NP_int>);
}

void test_shared_sub_lattice_distinct_wrapper() {
    // RecipeSpec and NumericalTier BOTH key on Tolerance — RecipeSpec
    // carries it as a runtime value (axis 1 of its product lattice),
    // NumericalTier pins it as a compile-time NTTP.  The two wrappers
    // have COMPLETELY DIFFERENT shapes:
    //
    //     RecipeSpec<int>                  template<T>
    //                                      runtime grade
    //     NumericalTier<BITEXACT, int>     template<NTTP, T>
    //                                      compile-time grade
    //
    // Pin that sharing a sub-lattice does NOT collapse the wrapper
    // identities — a sub-lattice rename or schema migration that
    // accidentally unifies them must break here.
    static_assert(!std::is_same_v<RS_int, NT_int_bitexact>);
    static_assert( extract::is_recipe_spec_v<RS_int>);
    static_assert(!extract::is_recipe_spec_v<NT_int_bitexact>);
    static_assert(!extract::is_numerical_tier_v<RS_int>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact>);
}

void test_public_aliases_round_trip() {
    using RecipeSpecInt    = safety::RecipeSpec<int>;
    using RecipeSpecDouble = safety::RecipeSpec<double>;

    static_assert( extract::is_recipe_spec_v<RecipeSpecInt>);
    static_assert( extract::is_recipe_spec_v<RecipeSpecDouble>);
    static_assert(std::is_same_v<RecipeSpecInt,    RS_int>);
    static_assert(std::is_same_v<RecipeSpecDouble, RS_double>);

    static_assert(std::is_same_v<
        extract::recipe_spec_value_t<RecipeSpecInt>, int>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<RS_int>);
    static_assert( extract::is_graded_wrapper_v<RS_double>);
    static_assert( extract::is_graded_wrapper_v<RS_uint64>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<RS_int>,
        extract::recipe_spec_value_t<RS_int>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_recipe_spec_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfRs = safety::Consistency<
        extract::Consistency_v::STRONG, RS_int>;
    using RsOfConsistency = safety::RecipeSpec<Cn_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfRs>);
    static_assert(!extract::is_recipe_spec_v<ConsistencyOfRs>);
    static_assert( extract::is_recipe_spec_v<
        extract::consistency_value_t<ConsistencyOfRs>>);

    static_assert( extract::is_recipe_spec_v<RsOfConsistency>);
    static_assert(!extract::is_consistency_v<RsOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::recipe_spec_value_t<RsOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // RecipeSpec vs all 12 other shipped wrappers — fully disjoint.
    // This is the fullest cross-wrapper exclusion test in the D30
    // batch, closing the wrapper-identity guarantee for the entire
    // D series.
    static_assert( extract::is_recipe_spec_v<RS_int>);
    static_assert(!extract::is_owned_region_v<RS_int>);
    static_assert(!extract::is_numerical_tier_v<RS_int>);
    static_assert(!extract::is_consistency_v<RS_int>);
    static_assert(!extract::is_opaque_lifetime_v<RS_int>);
    static_assert(!extract::is_det_safe_v<RS_int>);
    static_assert(!extract::is_cipher_tier_v<RS_int>);
    static_assert(!extract::is_residency_heat_v<RS_int>);
    static_assert(!extract::is_vendor_v<RS_int>);
    static_assert(!extract::is_crash_v<RS_int>);
    static_assert(!extract::is_budgeted_v<RS_int>);
    static_assert(!extract::is_epoch_versioned_v<RS_int>);
    static_assert(!extract::is_numa_placement_v<RS_int>);

    static_assert(!extract::is_recipe_spec_v<OR_int_test>);
    static_assert(!extract::is_recipe_spec_v<NT_int_bitexact>);
    static_assert(!extract::is_recipe_spec_v<Cn_int_strong>);
    static_assert(!extract::is_recipe_spec_v<OL_int_fleet>);
    static_assert(!extract::is_recipe_spec_v<DS_int_pure>);
    static_assert(!extract::is_recipe_spec_v<CT_int_hot>);
    static_assert(!extract::is_recipe_spec_v<RH_int_hot>);
    static_assert(!extract::is_recipe_spec_v<V_int_nv>);
    static_assert(!extract::is_recipe_spec_v<C_int_no_throw>);
    static_assert(!extract::is_recipe_spec_v<B_int>);
    static_assert(!extract::is_recipe_spec_v<EV_int>);
    static_assert(!extract::is_recipe_spec_v<NP_int>);

    static_assert(!extract::is_recipe_spec_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_recipe_spec_v<RS_int>;
    bool baseline_neg = !extract::is_recipe_spec_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_recipe_spec_v<RS_int>);
        EXPECT_TRUE(baseline_neg == !extract::is_recipe_spec_v<int>);
        EXPECT_TRUE(extract::IsRecipeSpec<RS_double&&>);
        EXPECT_TRUE(!extract::IsRecipeSpec<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_recipe_spec:\n");
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
    run_test("test_positive_nested_recipe_spec",
             test_positive_nested_recipe_spec);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip", test_local_alias_round_trip);
    run_test("test_runtime_grade_layout_invariant",
             test_runtime_grade_layout_invariant);
    run_test("test_three_distinct_product_wrapper_layouts",
             test_three_distinct_product_wrapper_layouts);
    run_test("test_shared_sub_lattice_distinct_wrapper",
             test_shared_sub_lattice_distinct_wrapper);
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
