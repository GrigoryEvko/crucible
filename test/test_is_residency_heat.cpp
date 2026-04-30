// ═══════════════════════════════════════════════════════════════════
// test_is_residency_heat — sentinel TU for safety/IsResidencyHeat.h
//
// FOUND-D30 (second wrapper of batch).  Mirror of test_is_cipher_tier
// applied to ResidencyHeat<Tier, T>.  Same 3-tier (Cold/Warm/Hot)
// shape; full audit-extended template.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsResidencyHeat.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/ResidencyHeat.h>
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

using RH_int_hot      = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, int>;
using RH_int_warm     = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Warm, int>;
using RH_int_cold     = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Cold, int>;
using RH_double_hot   = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, double>;
using RH_float_cold   = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Cold, float>;

struct test_tag {};
using OR_int_test     = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using C_int_strong    = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using DS_int_pure     = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using CT_int_hot      = safety::CipherTier<extract::CipherTierTag_v::Hot, int>;

struct payload_struct { int a; double b; };
using RH_struct_hot = safety::ResidencyHeat<
    extract::ResidencyHeatTag_v::Hot, payload_struct>;
using RH_nested = safety::ResidencyHeat<
    extract::ResidencyHeatTag_v::Warm, RH_int_hot>;

}  // namespace

namespace rh_test {
void f_takes_hot_int(RH_int_hot const&) noexcept;
void f_takes_cold_int(RH_int_cold&&) noexcept;
RH_double_hot f_returns_hot_double(int) noexcept;
}  // namespace rh_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_residency_heat_smoke_test());
}

void test_positive_every_tier() {
    static_assert( extract::is_residency_heat_v<RH_int_hot>);
    static_assert( extract::is_residency_heat_v<RH_int_warm>);
    static_assert( extract::is_residency_heat_v<RH_int_cold>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_residency_heat_v<RH_double_hot>);
    static_assert( extract::is_residency_heat_v<RH_float_cold>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_residency_heat_v<RH_int_hot>);
    static_assert( extract::is_residency_heat_v<RH_int_hot&>);
    static_assert( extract::is_residency_heat_v<RH_int_hot&&>);
    static_assert( extract::is_residency_heat_v<RH_int_hot const&>);
    static_assert( extract::is_residency_heat_v<RH_int_hot const>);
    static_assert( extract::is_residency_heat_v<RH_int_hot volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_residency_heat_v<int>);
    static_assert(!extract::is_residency_heat_v<double>);
    static_assert(!extract::is_residency_heat_v<void>);
    static_assert(!extract::is_residency_heat_v<extract::ResidencyHeatTag_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_residency_heat_v<int*>);
    static_assert(!extract::is_residency_heat_v<RH_int_hot*>);
    static_assert(!extract::is_residency_heat_v<RH_int_hot[5]>);
    static_assert(!extract::is_residency_heat_v<RH_int_hot* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeRH { int value; extract::ResidencyHeatTag_v tier; };
    static_assert(!extract::is_residency_heat_v<LookalikeRH>);
}

void test_concept_form_in_constraints() {
    auto callable_with_rh = []<typename T>()
        requires extract::IsResidencyHeat<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_rh.template operator()<RH_int_hot>());
    EXPECT_TRUE(callable_with_rh.template operator()<RH_int_cold>());
    EXPECT_TRUE(callable_with_rh.template operator()<RH_double_hot>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_int_hot>, int>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_int_cold>, int>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_double_hot>, double>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_float_cold>, float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_int_hot const&>, int>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_int_hot&&>, int>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_double_hot const>, double>);
}

void test_tag_extraction_every_value() {
    static_assert(extract::residency_heat_tag_v<RH_int_hot>
                  == extract::ResidencyHeatTag_v::Hot);
    static_assert(extract::residency_heat_tag_v<RH_int_warm>
                  == extract::ResidencyHeatTag_v::Warm);
    static_assert(extract::residency_heat_tag_v<RH_int_cold>
                  == extract::ResidencyHeatTag_v::Cold);
}

void test_tag_extraction_cv_ref_strips() {
    static_assert(extract::residency_heat_tag_v<RH_int_hot const&>
                  == extract::ResidencyHeatTag_v::Hot);
    static_assert(extract::residency_heat_tag_v<RH_int_cold&&>
                  == extract::ResidencyHeatTag_v::Cold);
}

void test_distinct_tiers_distinct_specs() {
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_int_hot>,
        extract::residency_heat_value_t<RH_int_cold>>);
    static_assert(extract::residency_heat_tag_v<RH_int_hot>
                  != extract::residency_heat_tag_v<RH_int_cold>);

    static_assert(extract::residency_heat_tag_v<RH_int_hot>
                  == extract::residency_heat_tag_v<RH_double_hot>);
    static_assert(!std::is_same_v<
        extract::residency_heat_value_t<RH_int_hot>,
        extract::residency_heat_value_t<RH_double_hot>>);
}

void test_dispatcher_integration_example() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsResidencyHeat<T>
    {
        constexpr auto tag = extract::residency_heat_tag_v<T>;
        if constexpr (tag == extract::ResidencyHeatTag_v::Hot) {
            return 3;  // L1 / hot kernel cache
        } else if constexpr (tag == extract::ResidencyHeatTag_v::Warm) {
            return 2;  // L2 / IR003 region
        } else {
            return 1;  // Cold / IR002 region
        }
    };

    static_assert(select_lowering.template operator()<RH_int_hot>()  == 3);
    static_assert(select_lowering.template operator()<RH_int_warm>() == 2);
    static_assert(select_lowering.template operator()<RH_int_cold>() == 1);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_residency_heat_v<RH_struct_hot>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_struct_hot>, payload_struct>);
    static_assert(extract::residency_heat_tag_v<RH_struct_hot>
                  == extract::ResidencyHeatTag_v::Hot);
}

void test_positive_nested_residency_heat() {
    static_assert( extract::is_residency_heat_v<RH_nested>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<RH_nested>, RH_int_hot>);
    static_assert( extract::is_residency_heat_v<
        extract::residency_heat_value_t<RH_nested>>);
    static_assert(std::is_same_v<
        extract::residency_heat_value_t<
            extract::residency_heat_value_t<RH_nested>>,
        int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&rh_test::f_takes_hot_int, 0>;
    static_assert( ex::is_residency_heat_v<P0>);
    static_assert(ex::residency_heat_tag_v<P0>
                  == ex::ResidencyHeatTag_v::Hot);
    static_assert(std::is_same_v<ex::residency_heat_value_t<P0>, int>);

    using P0_cold = ex::param_type_t<&rh_test::f_takes_cold_int, 0>;
    static_assert( ex::is_residency_heat_v<P0_cold>);
    static_assert(ex::residency_heat_tag_v<P0_cold>
                  == ex::ResidencyHeatTag_v::Cold);

    using R = ex::return_type_t<&rh_test::f_returns_hot_double>;
    static_assert( ex::is_residency_heat_v<R>);
    static_assert(std::is_same_v<ex::residency_heat_value_t<R>, double>);
    static_assert(ex::residency_heat_tag_v<R> == ex::ResidencyHeatTag_v::Hot);
}

void test_local_alias_round_trip() {
    using LocalHotInt = safety::ResidencyHeat<
        extract::ResidencyHeatTag_v::Hot, int>;

    static_assert( extract::is_residency_heat_v<LocalHotInt>);
    static_assert(extract::residency_heat_tag_v<LocalHotInt>
                  == extract::ResidencyHeatTag_v::Hot);
    static_assert(std::is_same_v<LocalHotInt, RH_int_hot>);
}

void test_zero_cost_layout_invariant() {
    static_assert(sizeof(RH_int_hot)    == sizeof(int));
    static_assert(sizeof(RH_int_cold)   == sizeof(int));
    static_assert(sizeof(RH_double_hot) == sizeof(double));
    static_assert(sizeof(RH_struct_hot) == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    using HotInt    = safety::residency_heat::Hot<int>;
    using WarmInt   = safety::residency_heat::Warm<int>;
    using ColdInt   = safety::residency_heat::Cold<int>;
    using HotDouble = safety::residency_heat::Hot<double>;

    static_assert( extract::is_residency_heat_v<HotInt>);
    static_assert( extract::is_residency_heat_v<WarmInt>);
    static_assert( extract::is_residency_heat_v<ColdInt>);
    static_assert( extract::is_residency_heat_v<HotDouble>);

    static_assert(extract::residency_heat_tag_v<HotInt>
                  == extract::ResidencyHeatTag_v::Hot);
    static_assert(extract::residency_heat_tag_v<WarmInt>
                  == extract::ResidencyHeatTag_v::Warm);
    static_assert(extract::residency_heat_tag_v<ColdInt>
                  == extract::ResidencyHeatTag_v::Cold);

    static_assert(std::is_same_v<HotInt, RH_int_hot>);
    static_assert(std::is_same_v<ColdInt, RH_int_cold>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<RH_int_hot>);
    static_assert( extract::is_graded_wrapper_v<RH_double_hot>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<RH_int_hot>,
        extract::residency_heat_value_t<RH_int_hot>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_residency_heat_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfRH = safety::Consistency<
        extract::Consistency_v::STRONG, RH_int_hot>;
    using RHOfConsistency = safety::ResidencyHeat<
        extract::ResidencyHeatTag_v::Hot, C_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfRH>);
    static_assert(!extract::is_residency_heat_v<ConsistencyOfRH>);
    static_assert( extract::is_residency_heat_v<
        extract::consistency_value_t<ConsistencyOfRH>>);

    static_assert( extract::is_residency_heat_v<RHOfConsistency>);
    static_assert(!extract::is_consistency_v<RHOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::residency_heat_value_t<RHOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // ResidencyHeat vs all 6 other shipped wrappers — fully disjoint.
    static_assert( extract::is_residency_heat_v<RH_int_hot>);
    static_assert(!extract::is_owned_region_v<RH_int_hot>);
    static_assert(!extract::is_numerical_tier_v<RH_int_hot>);
    static_assert(!extract::is_consistency_v<RH_int_hot>);
    static_assert(!extract::is_opaque_lifetime_v<RH_int_hot>);
    static_assert(!extract::is_det_safe_v<RH_int_hot>);
    static_assert(!extract::is_cipher_tier_v<RH_int_hot>);

    static_assert(!extract::is_residency_heat_v<OR_int_test>);
    static_assert(!extract::is_residency_heat_v<NT_int_bitexact>);
    static_assert(!extract::is_residency_heat_v<C_int_strong>);
    static_assert(!extract::is_residency_heat_v<OL_int_fleet>);
    static_assert(!extract::is_residency_heat_v<DS_int_pure>);
    static_assert(!extract::is_residency_heat_v<CT_int_hot>);

    static_assert(!extract::is_residency_heat_v<int>);
}

void test_cipher_tier_vs_residency_heat_distinct() {
    // CipherTier and ResidencyHeat both have a 3-tier (Cold/Warm/Hot)
    // shape, both are NTTP-templated on a Tag enum.  They are
    // STRUCTURALLY identical to a casual reading but are DIFFERENT
    // C++ types — the partial specs key on the wrapper class itself,
    // not on the lattice or enum.  Pin this so a refactor that
    // accidentally unifies the partial specs (e.g., via a CRTP base)
    // breaks loudly.
    static_assert(!std::is_same_v<RH_int_hot, CT_int_hot>);
    static_assert( extract::is_residency_heat_v<RH_int_hot>);
    static_assert(!extract::is_residency_heat_v<CT_int_hot>);
    static_assert( extract::is_cipher_tier_v<CT_int_hot>);
    static_assert(!extract::is_cipher_tier_v<RH_int_hot>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_residency_heat_v<RH_int_hot>;
    bool baseline_neg = !extract::is_residency_heat_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_residency_heat_v<RH_int_hot>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_residency_heat_v<int>);
        EXPECT_TRUE(extract::IsResidencyHeat<RH_int_hot&&>);
        EXPECT_TRUE(!extract::IsResidencyHeat<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_residency_heat:\n");
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
    run_test("test_tag_extraction_every_value",
             test_tag_extraction_every_value);
    run_test("test_tag_extraction_cv_ref_strips",
             test_tag_extraction_cv_ref_strips);
    run_test("test_distinct_tiers_distinct_specs",
             test_distinct_tiers_distinct_specs);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_residency_heat",
             test_positive_nested_residency_heat);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip", test_local_alias_round_trip);
    run_test("test_zero_cost_layout_invariant",
             test_zero_cost_layout_invariant);
    run_test("test_public_convenience_aliases",
             test_public_convenience_aliases);
    run_test("test_graded_wrapper_conformance",
             test_graded_wrapper_conformance);
    run_test("test_outer_wrapper_identity_dominates_nested",
             test_outer_wrapper_identity_dominates_nested);
    run_test("test_cross_wrapper_exclusion", test_cross_wrapper_exclusion);
    run_test("test_cipher_tier_vs_residency_heat_distinct",
             test_cipher_tier_vs_residency_heat_distinct);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
