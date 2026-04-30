// ═══════════════════════════════════════════════════════════════════
// test_is_opaque_lifetime — sentinel TU for safety/IsOpaqueLifetime.h
//
// FOUND-D23: third wrapper-detector for the FOUND-G product
// wrappers.  Mechanical extension of D21/D22 with audit-extended
// coverage from the start (layout invariant + public aliases +
// GradedWrapper conformance).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsOpaqueLifetime.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
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

// All 3 Lifetime_v scopes.
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using OL_int_program  = safety::OpaqueLifetime<extract::Lifetime_v::PER_PROGRAM, int>;
using OL_int_request  = safety::OpaqueLifetime<extract::Lifetime_v::PER_REQUEST, int>;
using OL_double_fleet = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, double>;
using OL_float_request = safety::OpaqueLifetime<extract::Lifetime_v::PER_REQUEST, float>;

// Cross-wrapper exclusion witnesses.
struct test_tag {};
using OR_int_test = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using C_int_strong = safety::Consistency<extract::Consistency_v::STRONG, int>;

// Non-fundamental and nested.
struct payload_struct { int a; double b; };
using OL_struct_fleet = safety::OpaqueLifetime<
    extract::Lifetime_v::PER_FLEET, payload_struct>;
using OL_nested = safety::OpaqueLifetime<
    extract::Lifetime_v::PER_PROGRAM, OL_int_fleet>;

}  // namespace

namespace ol_test {
void f_takes_fleet_int(OL_int_fleet const&) noexcept;
void f_takes_request_int(OL_int_request&&) noexcept;
OL_double_fleet f_returns_fleet_double(int) noexcept;
}  // namespace ol_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_opaque_lifetime_smoke_test());
}

void test_positive_every_scope() {
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_program>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_request>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_opaque_lifetime_v<OL_double_fleet>);
    static_assert( extract::is_opaque_lifetime_v<OL_float_request>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet&>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet&&>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet const&>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet const>);
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_opaque_lifetime_v<int>);
    static_assert(!extract::is_opaque_lifetime_v<double>);
    static_assert(!extract::is_opaque_lifetime_v<void>);
    static_assert(!extract::is_opaque_lifetime_v<extract::Lifetime_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_opaque_lifetime_v<int*>);
    static_assert(!extract::is_opaque_lifetime_v<OL_int_fleet*>);
    static_assert(!extract::is_opaque_lifetime_v<OL_int_fleet[5]>);
    static_assert(!extract::is_opaque_lifetime_v<OL_int_fleet* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeLifetime { int value; extract::Lifetime_v scope; };
    static_assert(!extract::is_opaque_lifetime_v<LookalikeLifetime>);
}

void test_concept_form_in_constraints() {
    auto callable_with_ol = []<typename T>()
        requires extract::IsOpaqueLifetime<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_ol.template operator()<OL_int_fleet>());
    EXPECT_TRUE(callable_with_ol.template operator()<OL_int_request>());
    EXPECT_TRUE(callable_with_ol.template operator()<OL_double_fleet>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_fleet>, int>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_request>, int>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_double_fleet>, double>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_float_request>, float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_fleet const&>, int>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_fleet&&>, int>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_double_fleet const>, double>);
}

void test_scope_extraction_every_value() {
    // All 3 Lifetime_v values round-trip.
    static_assert(extract::opaque_lifetime_scope_v<OL_int_fleet>
                  == extract::Lifetime_v::PER_FLEET);
    static_assert(extract::opaque_lifetime_scope_v<OL_int_program>
                  == extract::Lifetime_v::PER_PROGRAM);
    static_assert(extract::opaque_lifetime_scope_v<OL_int_request>
                  == extract::Lifetime_v::PER_REQUEST);
}

void test_scope_extraction_cv_ref_strips() {
    static_assert(extract::opaque_lifetime_scope_v<OL_int_fleet const&>
                  == extract::Lifetime_v::PER_FLEET);
    static_assert(extract::opaque_lifetime_scope_v<OL_int_request&&>
                  == extract::Lifetime_v::PER_REQUEST);
}

void test_distinct_scopes_distinct_specs() {
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_fleet>,
        extract::opaque_lifetime_value_t<OL_int_request>>);  // both int
    static_assert(extract::opaque_lifetime_scope_v<OL_int_fleet>
                  != extract::opaque_lifetime_scope_v<OL_int_request>);

    static_assert(extract::opaque_lifetime_scope_v<OL_int_fleet>
                  == extract::opaque_lifetime_scope_v<OL_double_fleet>);
    static_assert(!std::is_same_v<
        extract::opaque_lifetime_value_t<OL_int_fleet>,
        extract::opaque_lifetime_value_t<OL_double_fleet>>);
}

void test_dispatcher_integration_example() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsOpaqueLifetime<T>
    {
        constexpr auto scope = extract::opaque_lifetime_scope_v<T>;
        if constexpr (scope == extract::Lifetime_v::PER_FLEET) {
            return 3;  // longest-lived
        } else if constexpr (scope == extract::Lifetime_v::PER_PROGRAM) {
            return 2;
        } else {
            return 1;  // PER_REQUEST — shortest-lived
        }
    };

    static_assert(select_lowering.template operator()<OL_int_fleet>()   == 3);
    static_assert(select_lowering.template operator()<OL_int_program>() == 2);
    static_assert(select_lowering.template operator()<OL_int_request>() == 1);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_opaque_lifetime_v<OL_struct_fleet>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_struct_fleet>, payload_struct>);
    static_assert(extract::opaque_lifetime_scope_v<OL_struct_fleet>
                  == extract::Lifetime_v::PER_FLEET);
}

void test_positive_nested_opaque_lifetime() {
    static_assert( extract::is_opaque_lifetime_v<OL_nested>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<OL_nested>, OL_int_fleet>);
    static_assert( extract::is_opaque_lifetime_v<
        extract::opaque_lifetime_value_t<OL_nested>>);
    static_assert(std::is_same_v<
        extract::opaque_lifetime_value_t<
            extract::opaque_lifetime_value_t<OL_nested>>,
        int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&ol_test::f_takes_fleet_int, 0>;
    static_assert( ex::is_opaque_lifetime_v<P0>);
    static_assert(ex::opaque_lifetime_scope_v<P0>
                  == ex::Lifetime_v::PER_FLEET);
    static_assert(std::is_same_v<ex::opaque_lifetime_value_t<P0>, int>);

    using P0_request = ex::param_type_t<&ol_test::f_takes_request_int, 0>;
    static_assert( ex::is_opaque_lifetime_v<P0_request>);
    static_assert(ex::opaque_lifetime_scope_v<P0_request>
                  == ex::Lifetime_v::PER_REQUEST);

    using R = ex::return_type_t<&ol_test::f_returns_fleet_double>;
    static_assert( ex::is_opaque_lifetime_v<R>);
    static_assert(std::is_same_v<ex::opaque_lifetime_value_t<R>, double>);
    static_assert(ex::opaque_lifetime_scope_v<R>
                  == ex::Lifetime_v::PER_FLEET);
}

void test_local_alias_round_trip() {
    using LocalFleetInt = safety::OpaqueLifetime<
        extract::Lifetime_v::PER_FLEET, int>;

    static_assert( extract::is_opaque_lifetime_v<LocalFleetInt>);
    static_assert(extract::opaque_lifetime_scope_v<LocalFleetInt>
                  == extract::Lifetime_v::PER_FLEET);
    static_assert(std::is_same_v<LocalFleetInt, OL_int_fleet>);
}

void test_zero_cost_layout_invariant() {
    static_assert(sizeof(OL_int_fleet)    == sizeof(int));
    static_assert(sizeof(OL_int_request)  == sizeof(int));
    static_assert(sizeof(OL_double_fleet) == sizeof(double));
    static_assert(sizeof(OL_struct_fleet) == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    // safety::opaque_lifetime::PerFleet<T> etc. are PUBLIC aliases.
    using FleetInt    = safety::opaque_lifetime::PerFleet<int>;
    using ProgramInt  = safety::opaque_lifetime::PerProgram<int>;
    using RequestInt  = safety::opaque_lifetime::PerRequest<int>;
    using FleetDouble = safety::opaque_lifetime::PerFleet<double>;

    static_assert( extract::is_opaque_lifetime_v<FleetInt>);
    static_assert( extract::is_opaque_lifetime_v<ProgramInt>);
    static_assert( extract::is_opaque_lifetime_v<RequestInt>);
    static_assert( extract::is_opaque_lifetime_v<FleetDouble>);

    static_assert(extract::opaque_lifetime_scope_v<FleetInt>
                  == extract::Lifetime_v::PER_FLEET);
    static_assert(extract::opaque_lifetime_scope_v<ProgramInt>
                  == extract::Lifetime_v::PER_PROGRAM);
    static_assert(extract::opaque_lifetime_scope_v<RequestInt>
                  == extract::Lifetime_v::PER_REQUEST);

    // Aliases ARE the SAME TYPE as expanded forms.
    static_assert(std::is_same_v<FleetInt, OL_int_fleet>);
    static_assert(std::is_same_v<RequestInt, OL_int_request>);
}

void test_graded_wrapper_conformance() {
    // OpaqueLifetime satisfies the GradedWrapper concept (FOUND-G09).
    static_assert( extract::is_graded_wrapper_v<OL_int_fleet>);
    static_assert( extract::is_graded_wrapper_v<OL_double_fleet>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<OL_int_fleet>,
        extract::opaque_lifetime_value_t<OL_int_fleet>>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<OL_double_fleet>,
        extract::opaque_lifetime_value_t<OL_double_fleet>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_opaque_lifetime_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    // Composition discipline: when wrappers nest, the OUTER
    // wrapper's identity is determined ONLY by the outer template.
    // Inner wrappers do NOT "leak through" to the outside.
    //
    //   Consistency<Strong, OpaqueLifetime<Fleet, int>>
    //     → IS Consistency
    //     → is NOT OpaqueLifetime
    //
    //   OpaqueLifetime<Fleet, Consistency<Strong, int>>
    //     → IS OpaqueLifetime
    //     → is NOT Consistency
    //
    // Catches a refactor that accidentally writes a partial spec
    // matching ANY wrapper of an Opaque-shaped value (would be
    // semantically wrong).

    using ConsistencyOfLifetime = safety::Consistency<
        extract::Consistency_v::STRONG, OL_int_fleet>;
    using LifetimeOfConsistency = safety::OpaqueLifetime<
        extract::Lifetime_v::PER_FLEET, C_int_strong>;

    // Outer Consistency wraps OpaqueLifetime — outer wins.
    static_assert( extract::is_consistency_v<ConsistencyOfLifetime>);
    static_assert(!extract::is_opaque_lifetime_v<ConsistencyOfLifetime>);
    // Inner is reachable via the value-type extraction.
    static_assert( extract::is_opaque_lifetime_v<
        extract::consistency_value_t<ConsistencyOfLifetime>>);

    // Outer OpaqueLifetime wraps Consistency — outer wins.
    static_assert( extract::is_opaque_lifetime_v<LifetimeOfConsistency>);
    static_assert(!extract::is_consistency_v<LifetimeOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::opaque_lifetime_value_t<LifetimeOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // OpaqueLifetime vs OwnedRegion vs NumericalTier vs Consistency
    // — fully disjoint.
    static_assert( extract::is_opaque_lifetime_v<OL_int_fleet>);
    static_assert(!extract::is_owned_region_v<OL_int_fleet>);
    static_assert(!extract::is_numerical_tier_v<OL_int_fleet>);
    static_assert(!extract::is_consistency_v<OL_int_fleet>);

    static_assert(!extract::is_opaque_lifetime_v<OR_int_test>);
    static_assert(!extract::is_opaque_lifetime_v<NT_int_bitexact>);
    static_assert(!extract::is_opaque_lifetime_v<C_int_strong>);

    static_assert(!extract::is_opaque_lifetime_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_opaque_lifetime_v<OL_int_fleet>;
    bool baseline_neg = !extract::is_opaque_lifetime_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_opaque_lifetime_v<OL_int_fleet>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_opaque_lifetime_v<int>);
        EXPECT_TRUE(extract::IsOpaqueLifetime<OL_int_fleet&&>);
        EXPECT_TRUE(!extract::IsOpaqueLifetime<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_opaque_lifetime:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_every_scope", test_positive_every_scope);
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
    run_test("test_scope_extraction_every_value",
             test_scope_extraction_every_value);
    run_test("test_scope_extraction_cv_ref_strips",
             test_scope_extraction_cv_ref_strips);
    run_test("test_distinct_scopes_distinct_specs",
             test_distinct_scopes_distinct_specs);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_opaque_lifetime",
             test_positive_nested_opaque_lifetime);
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
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
