// ═══════════════════════════════════════════════════════════════════
// test_is_numa_placement — sentinel TU for safety/IsNumaPlacement.h
//
// FOUND-D30 (seventh wrapper of batch — third product wrapper).
//
// Mirror of test_is_budgeted/test_is_epoch_versioned.  Distinguishing
// axis: NumaPlacement's runtime grade is 40 bytes (1B NumaNodeId +
// 7B pad + 32B AffinityMask), NOT 16 bytes like the prior two.  The
// layout assertion uses the larger overhead and an additional test
// pins the cross-product-wrapper layout disagreement (NP > B vs same
// wrapped element).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsNumaPlacement.h>

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
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/NumaPlacement.h>
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

using NP_int      = safety::NumaPlacement<int>;
using NP_double   = safety::NumaPlacement<double>;
using NP_char     = safety::NumaPlacement<char>;
using NP_uint64   = safety::NumaPlacement<std::uint64_t>;
using NP_float    = safety::NumaPlacement<float>;

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

struct payload_struct { int a; double b; };
using NP_struct = safety::NumaPlacement<payload_struct>;
using NP_nested = safety::NumaPlacement<NP_int>;

}  // namespace

namespace np_test {
void f_takes_np_int(NP_int const&) noexcept;
void f_takes_np_double(NP_double&&) noexcept;
NP_uint64 f_returns_np_uint64(int) noexcept;
}  // namespace np_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_numa_placement_smoke_test());
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_numa_placement_v<NP_int>);
    static_assert( extract::is_numa_placement_v<NP_double>);
    static_assert( extract::is_numa_placement_v<NP_char>);
    static_assert( extract::is_numa_placement_v<NP_uint64>);
    static_assert( extract::is_numa_placement_v<NP_float>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_numa_placement_v<NP_int>);
    static_assert( extract::is_numa_placement_v<NP_int&>);
    static_assert( extract::is_numa_placement_v<NP_int&&>);
    static_assert( extract::is_numa_placement_v<NP_int const&>);
    static_assert( extract::is_numa_placement_v<NP_int const>);
    static_assert( extract::is_numa_placement_v<NP_int volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_numa_placement_v<int>);
    static_assert(!extract::is_numa_placement_v<double>);
    static_assert(!extract::is_numa_placement_v<void>);
    static_assert(!extract::is_numa_placement_v<safety::NumaNodeId>);
    static_assert(!extract::is_numa_placement_v<safety::AffinityMask>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_numa_placement_v<int*>);
    static_assert(!extract::is_numa_placement_v<NP_int*>);
    static_assert(!extract::is_numa_placement_v<NP_int[5]>);
    static_assert(!extract::is_numa_placement_v<NP_int* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeNumaPlacement {
        int value;
        safety::NumaNodeId node_field;
        safety::AffinityMask aff_field;
    };
    static_assert(!extract::is_numa_placement_v<LookalikeNumaPlacement>);
}

void test_concept_form_in_constraints() {
    auto callable_with_np = []<typename T>()
        requires extract::IsNumaPlacement<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_np.template operator()<NP_int>());
    EXPECT_TRUE(callable_with_np.template operator()<NP_double>());
    EXPECT_TRUE(callable_with_np.template operator()<NP_uint64>());
    EXPECT_TRUE(callable_with_np.template operator()<NP_struct>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::numa_placement_value_t<NP_int>,    int>);
    static_assert(std::is_same_v<extract::numa_placement_value_t<NP_double>, double>);
    static_assert(std::is_same_v<extract::numa_placement_value_t<NP_char>,   char>);
    static_assert(std::is_same_v<extract::numa_placement_value_t<NP_uint64>, std::uint64_t>);
    static_assert(std::is_same_v<extract::numa_placement_value_t<NP_float>,  float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NP_int const&>, int>);
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NP_int&&>, int>);
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NP_double const>, double>);
}

void test_distinct_element_types_distinct_specs() {
    static_assert(!std::is_same_v<NP_int, NP_double>);
    static_assert(!std::is_same_v<
        extract::numa_placement_value_t<NP_int>,
        extract::numa_placement_value_t<NP_double>>);
    using NP_int_alt = safety::NumaPlacement<int>;
    static_assert(std::is_same_v<NP_int, NP_int_alt>);
}

void test_runtime_grade_independence_from_type_identity() {
    NP_int np_default{};
    NP_int np_explicit{0, safety::NumaNodeId::Any,
                       safety::AffinityMask{}};
    static_assert(std::is_same_v<decltype(np_default), decltype(np_explicit)>);

    EXPECT_TRUE(extract::is_numa_placement_v<decltype(np_default)>);
    EXPECT_TRUE(extract::is_numa_placement_v<decltype(np_explicit)>);
}

void test_dispatcher_value_type_use_case() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsNumaPlacement<T>
    {
        using V = extract::numa_placement_value_t<T>;
        if constexpr (std::is_arithmetic_v<V>) return 1;
        else if constexpr (std::is_class_v<V>) return 2;
        else                                    return 0;
    };

    static_assert(select_lowering.template operator()<NP_int>()    == 1);
    static_assert(select_lowering.template operator()<NP_double>() == 1);
    static_assert(select_lowering.template operator()<NP_struct>() == 2);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_numa_placement_v<NP_struct>);
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NP_struct>, payload_struct>);
}

void test_positive_nested_numa_placement() {
    static_assert( extract::is_numa_placement_v<NP_nested>);
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NP_nested>, NP_int>);
    static_assert( extract::is_numa_placement_v<
        extract::numa_placement_value_t<NP_nested>>);
    static_assert(std::is_same_v<
        extract::numa_placement_value_t<
            extract::numa_placement_value_t<NP_nested>>, int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&np_test::f_takes_np_int, 0>;
    static_assert( ex::is_numa_placement_v<P0>);
    static_assert(std::is_same_v<ex::numa_placement_value_t<P0>, int>);

    using P0_double = ex::param_type_t<&np_test::f_takes_np_double, 0>;
    static_assert( ex::is_numa_placement_v<P0_double>);
    static_assert(std::is_same_v<ex::numa_placement_value_t<P0_double>, double>);

    using R = ex::return_type_t<&np_test::f_returns_np_uint64>;
    static_assert( ex::is_numa_placement_v<R>);
    static_assert(std::is_same_v<ex::numa_placement_value_t<R>, std::uint64_t>);
}

void test_local_alias_round_trip() {
    using LocalNpInt = safety::NumaPlacement<int>;
    static_assert( extract::is_numa_placement_v<LocalNpInt>);
    static_assert(std::is_same_v<LocalNpInt, NP_int>);
    static_assert(std::is_same_v<extract::numa_placement_value_t<LocalNpInt>, int>);
}

void test_runtime_grade_layout_invariant() {
    // DISTINGUISHED layout from Budgeted/EpochVersioned (16-byte
    // grade): NumaPlacement carries a 33-byte grade pair (1 byte
    // NumaNodeId + 32 byte AffinityMask), packed to 40 bytes with
    // 7 padding bytes after NumaNodeId for AffinityMask alignment.
    static_assert(sizeof(NP_int)    >= sizeof(int)    + 33);
    static_assert(sizeof(NP_double) >= sizeof(double) + 33);
    static_assert(sizeof(NP_char)   >= sizeof(char)   + 33);
    static_assert(sizeof(NP_struct) >= sizeof(payload_struct) + 33);

    // Contrast: NTTP wrapper still achieves sizeof == sizeof(T).
    static_assert(sizeof(NP_int) > sizeof(int));
    static_assert(sizeof(V_int_nv) == sizeof(int));
}

void test_distinct_product_wrapper_layouts() {
    // CROSS-PRODUCT-WRAPPER LAYOUT DISAGREEMENT.  Budgeted and
    // EpochVersioned share a 16-byte grade overhead (two uint64_t
    // axes), but NumaPlacement carries a 40-byte grade (32B
    // AffinityMask + 1B NumaNodeId + 7B alignment pad).  The detector
    // is indifferent to the size, but production code that iterates
    // over a tuple of product-wrappers must NOT assume uniform
    // grade size — pin the disagreement here.
    static_assert(sizeof(NP_int) > sizeof(B_int));
    static_assert(sizeof(NP_int) > sizeof(EV_int));
    static_assert(sizeof(B_int) == sizeof(EV_int));  // these two ARE equal

    // All three are distinct C++ types regardless of layout.
    static_assert(!std::is_same_v<NP_int, B_int>);
    static_assert(!std::is_same_v<NP_int, EV_int>);
    static_assert( extract::is_numa_placement_v<NP_int>);
    static_assert(!extract::is_numa_placement_v<B_int>);
    static_assert(!extract::is_numa_placement_v<EV_int>);
}

void test_public_aliases_round_trip() {
    using NumaPlacementInt    = safety::NumaPlacement<int>;
    using NumaPlacementDouble = safety::NumaPlacement<double>;

    static_assert( extract::is_numa_placement_v<NumaPlacementInt>);
    static_assert( extract::is_numa_placement_v<NumaPlacementDouble>);
    static_assert(std::is_same_v<NumaPlacementInt,    NP_int>);
    static_assert(std::is_same_v<NumaPlacementDouble, NP_double>);

    static_assert(std::is_same_v<
        extract::numa_placement_value_t<NumaPlacementInt>, int>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<NP_int>);
    static_assert( extract::is_graded_wrapper_v<NP_double>);
    static_assert( extract::is_graded_wrapper_v<NP_uint64>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<NP_int>,
        extract::numa_placement_value_t<NP_int>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_numa_placement_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfNp = safety::Consistency<
        extract::Consistency_v::STRONG, NP_int>;
    using NpOfConsistency = safety::NumaPlacement<Cn_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfNp>);
    static_assert(!extract::is_numa_placement_v<ConsistencyOfNp>);
    static_assert( extract::is_numa_placement_v<
        extract::consistency_value_t<ConsistencyOfNp>>);

    static_assert( extract::is_numa_placement_v<NpOfConsistency>);
    static_assert(!extract::is_consistency_v<NpOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::numa_placement_value_t<NpOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // NumaPlacement vs all 11 other shipped wrappers — fully disjoint.
    static_assert( extract::is_numa_placement_v<NP_int>);
    static_assert(!extract::is_owned_region_v<NP_int>);
    static_assert(!extract::is_numerical_tier_v<NP_int>);
    static_assert(!extract::is_consistency_v<NP_int>);
    static_assert(!extract::is_opaque_lifetime_v<NP_int>);
    static_assert(!extract::is_det_safe_v<NP_int>);
    static_assert(!extract::is_cipher_tier_v<NP_int>);
    static_assert(!extract::is_residency_heat_v<NP_int>);
    static_assert(!extract::is_vendor_v<NP_int>);
    static_assert(!extract::is_crash_v<NP_int>);
    static_assert(!extract::is_budgeted_v<NP_int>);
    static_assert(!extract::is_epoch_versioned_v<NP_int>);

    static_assert(!extract::is_numa_placement_v<OR_int_test>);
    static_assert(!extract::is_numa_placement_v<NT_int_bitexact>);
    static_assert(!extract::is_numa_placement_v<Cn_int_strong>);
    static_assert(!extract::is_numa_placement_v<OL_int_fleet>);
    static_assert(!extract::is_numa_placement_v<DS_int_pure>);
    static_assert(!extract::is_numa_placement_v<CT_int_hot>);
    static_assert(!extract::is_numa_placement_v<RH_int_hot>);
    static_assert(!extract::is_numa_placement_v<V_int_nv>);
    static_assert(!extract::is_numa_placement_v<C_int_no_throw>);
    static_assert(!extract::is_numa_placement_v<B_int>);
    static_assert(!extract::is_numa_placement_v<EV_int>);

    static_assert(!extract::is_numa_placement_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_numa_placement_v<NP_int>;
    bool baseline_neg = !extract::is_numa_placement_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_numa_placement_v<NP_int>);
        EXPECT_TRUE(baseline_neg == !extract::is_numa_placement_v<int>);
        EXPECT_TRUE(extract::IsNumaPlacement<NP_double&&>);
        EXPECT_TRUE(!extract::IsNumaPlacement<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_numa_placement:\n");
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
    run_test("test_positive_nested_numa_placement",
             test_positive_nested_numa_placement);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip", test_local_alias_round_trip);
    run_test("test_runtime_grade_layout_invariant",
             test_runtime_grade_layout_invariant);
    run_test("test_distinct_product_wrapper_layouts",
             test_distinct_product_wrapper_layouts);
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
