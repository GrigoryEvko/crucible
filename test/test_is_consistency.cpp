// ═══════════════════════════════════════════════════════════════════
// test_is_consistency — sentinel TU for safety/IsConsistency.h
//
// FOUND-D22: second wrapper-detector for the FOUND-G product
// wrappers.  Mechanical extension of D21's pattern.  Audit-extended
// from the start with all five gaps that D21's audit pass closed.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsConsistency.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOwnedRegion.h>
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

// All 5 Consistency_v levels.
using C_int_strong       = safety::Consistency<extract::Consistency_v::STRONG, int>;
using C_int_bounded      = safety::Consistency<extract::Consistency_v::BOUNDED_STALENESS, int>;
using C_int_causal       = safety::Consistency<extract::Consistency_v::CAUSAL_PREFIX, int>;
using C_int_ryw          = safety::Consistency<extract::Consistency_v::READ_YOUR_WRITES, int>;
using C_int_eventual     = safety::Consistency<extract::Consistency_v::EVENTUAL, int>;
using C_double_strong    = safety::Consistency<extract::Consistency_v::STRONG, double>;
using C_float_eventual   = safety::Consistency<extract::Consistency_v::EVENTUAL, float>;

// Cross-wrapper exclusion witnesses.
struct test_tag {};
using OR_int_test = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;

// Non-fundamental and nested cases.
struct payload_struct { int a; double b; };
using C_struct_strong = safety::Consistency<
    extract::Consistency_v::STRONG, payload_struct>;
using C_nested = safety::Consistency<
    extract::Consistency_v::CAUSAL_PREFIX, C_int_strong>;

}  // namespace

namespace cw_test {
void f_takes_strong_int(C_int_strong const&) noexcept;
void f_takes_eventual_int(C_int_eventual&&) noexcept;
C_double_strong f_returns_strong_double(int) noexcept;
}  // namespace cw_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_consistency_smoke_test());
}

void test_positive_every_level() {
    static_assert( extract::is_consistency_v<C_int_strong>);
    static_assert( extract::is_consistency_v<C_int_bounded>);
    static_assert( extract::is_consistency_v<C_int_causal>);
    static_assert( extract::is_consistency_v<C_int_ryw>);
    static_assert( extract::is_consistency_v<C_int_eventual>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_consistency_v<C_double_strong>);
    static_assert( extract::is_consistency_v<C_float_eventual>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_consistency_v<C_int_strong>);
    static_assert( extract::is_consistency_v<C_int_strong&>);
    static_assert( extract::is_consistency_v<C_int_strong&&>);
    static_assert( extract::is_consistency_v<C_int_strong const&>);
    static_assert( extract::is_consistency_v<C_int_strong const>);
    static_assert( extract::is_consistency_v<C_int_strong volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_consistency_v<int>);
    static_assert(!extract::is_consistency_v<double>);
    static_assert(!extract::is_consistency_v<void>);
    static_assert(!extract::is_consistency_v<extract::Consistency_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_consistency_v<int*>);
    static_assert(!extract::is_consistency_v<int**>);
    static_assert(!extract::is_consistency_v<C_int_strong*>);
    static_assert(!extract::is_consistency_v<C_int_strong[5]>);
    static_assert(!extract::is_consistency_v<C_int_strong* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeConsistency { int value; extract::Consistency_v level; };
    static_assert(!extract::is_consistency_v<LookalikeConsistency>);
}

void test_concept_form_in_constraints() {
    auto callable_with_c = []<typename T>()
        requires extract::IsConsistency<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_c.template operator()<C_int_strong>());
    EXPECT_TRUE(callable_with_c.template operator()<C_int_eventual>());
    EXPECT_TRUE(callable_with_c.template operator()<C_double_strong>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_int_strong>, int>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_int_eventual>, int>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_double_strong>, double>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_float_eventual>, float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_int_strong const&>, int>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_int_strong&&>, int>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_double_strong const>, double>);
}

void test_level_extraction_every_value() {
    // All 5 Consistency_v values round-trip through the trait.
    static_assert(extract::consistency_level_v<C_int_strong>
                  == extract::Consistency_v::STRONG);
    static_assert(extract::consistency_level_v<C_int_bounded>
                  == extract::Consistency_v::BOUNDED_STALENESS);
    static_assert(extract::consistency_level_v<C_int_causal>
                  == extract::Consistency_v::CAUSAL_PREFIX);
    static_assert(extract::consistency_level_v<C_int_ryw>
                  == extract::Consistency_v::READ_YOUR_WRITES);
    static_assert(extract::consistency_level_v<C_int_eventual>
                  == extract::Consistency_v::EVENTUAL);
}

void test_level_extraction_cv_ref_strips() {
    static_assert(extract::consistency_level_v<C_int_strong const&>
                  == extract::Consistency_v::STRONG);
    static_assert(extract::consistency_level_v<C_int_eventual&&>
                  == extract::Consistency_v::EVENTUAL);
}

void test_distinct_levels_distinct_specs() {
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_int_strong>,
        extract::consistency_value_t<C_int_eventual>>);  // both int
    static_assert(extract::consistency_level_v<C_int_strong>
                  != extract::consistency_level_v<C_int_eventual>);

    static_assert(extract::consistency_level_v<C_int_strong>
                  == extract::consistency_level_v<C_double_strong>);
    static_assert(!std::is_same_v<
        extract::consistency_value_t<C_int_strong>,
        extract::consistency_value_t<C_double_strong>>);
}

void test_dispatcher_integration_example() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsConsistency<T>
    {
        constexpr auto level = extract::consistency_level_v<T>;
        if constexpr (level == extract::Consistency_v::STRONG) {
            return 5;  // strictest
        } else if constexpr (level
                             == extract::Consistency_v::BOUNDED_STALENESS) {
            return 4;
        } else if constexpr (level
                             == extract::Consistency_v::CAUSAL_PREFIX) {
            return 3;
        } else if constexpr (level
                             == extract::Consistency_v::READ_YOUR_WRITES) {
            return 2;
        } else {
            return 1;  // EVENTUAL — most permissive
        }
    };

    static_assert(select_lowering.template operator()<C_int_strong>()   == 5);
    static_assert(select_lowering.template operator()<C_int_bounded>()  == 4);
    static_assert(select_lowering.template operator()<C_int_causal>()   == 3);
    static_assert(select_lowering.template operator()<C_int_ryw>()      == 2);
    static_assert(select_lowering.template operator()<C_int_eventual>() == 1);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_consistency_v<C_struct_strong>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_struct_strong>, payload_struct>);
    static_assert(extract::consistency_level_v<C_struct_strong>
                  == extract::Consistency_v::STRONG);
}

void test_positive_nested_consistency() {
    static_assert( extract::is_consistency_v<C_nested>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<C_nested>, C_int_strong>);
    // Inner is itself a Consistency — recursive detection works.
    static_assert( extract::is_consistency_v<
        extract::consistency_value_t<C_nested>>);
    // Recurse one more level.
    static_assert(std::is_same_v<
        extract::consistency_value_t<
            extract::consistency_value_t<C_nested>>,
        int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&cw_test::f_takes_strong_int, 0>;
    static_assert( ex::is_consistency_v<P0>);
    static_assert(ex::consistency_level_v<P0>
                  == ex::Consistency_v::STRONG);
    static_assert(std::is_same_v<ex::consistency_value_t<P0>, int>);

    using P0_eventual = ex::param_type_t<&cw_test::f_takes_eventual_int, 0>;
    static_assert( ex::is_consistency_v<P0_eventual>);
    static_assert(ex::consistency_level_v<P0_eventual>
                  == ex::Consistency_v::EVENTUAL);

    using R = ex::return_type_t<&cw_test::f_returns_strong_double>;
    static_assert( ex::is_consistency_v<R>);
    static_assert(std::is_same_v<ex::consistency_value_t<R>, double>);
    static_assert(ex::consistency_level_v<R> == ex::Consistency_v::STRONG);
}

void test_local_alias_round_trip() {
    using LocalStrongInt = safety::Consistency<
        extract::Consistency_v::STRONG, int>;
    using LocalCausalDouble = safety::Consistency<
        extract::Consistency_v::CAUSAL_PREFIX, double>;

    static_assert( extract::is_consistency_v<LocalStrongInt>);
    static_assert( extract::is_consistency_v<LocalCausalDouble>);
    static_assert(extract::consistency_level_v<LocalStrongInt>
                  == extract::Consistency_v::STRONG);
    static_assert(extract::consistency_level_v<LocalCausalDouble>
                  == extract::Consistency_v::CAUSAL_PREFIX);
}

void test_zero_cost_layout_invariant() {
    // The wrapper-detection trait must be a pure metaprogram — it
    // doesn't add storage to the wrapped value.  Pin the regime-1
    // EBO collapse claim from Consistency.h L342-347 from the
    // dispatcher-side perspective: if a function takes a
    // Consistency<L, T> by value, the ABI is the same as taking T.
    static_assert(sizeof(C_int_strong)   == sizeof(int));
    static_assert(sizeof(C_int_eventual) == sizeof(int));
    static_assert(sizeof(C_double_strong) == sizeof(double));
    static_assert(sizeof(C_struct_strong) == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    // safety::consistency::Strong<T> etc. are PUBLIC aliases (not
    // detail::).  They must round-trip through the trait identically
    // to their expanded forms.
    using StrongInt    = safety::consistency::Strong<int>;
    using EventualInt  = safety::consistency::Eventual<int>;
    using CausalDouble = safety::consistency::CausalPrefix<double>;
    using BoundedFloat = safety::consistency::BoundedStaleness<float>;
    using RywChar      = safety::consistency::ReadYourWrites<char>;

    // All aliases admitted.
    static_assert( extract::is_consistency_v<StrongInt>);
    static_assert( extract::is_consistency_v<EventualInt>);
    static_assert( extract::is_consistency_v<CausalDouble>);
    static_assert( extract::is_consistency_v<BoundedFloat>);
    static_assert( extract::is_consistency_v<RywChar>);

    // Levels recovered correctly through the alias.
    static_assert(extract::consistency_level_v<StrongInt>
                  == extract::Consistency_v::STRONG);
    static_assert(extract::consistency_level_v<EventualInt>
                  == extract::Consistency_v::EVENTUAL);
    static_assert(extract::consistency_level_v<CausalDouble>
                  == extract::Consistency_v::CAUSAL_PREFIX);
    static_assert(extract::consistency_level_v<BoundedFloat>
                  == extract::Consistency_v::BOUNDED_STALENESS);
    static_assert(extract::consistency_level_v<RywChar>
                  == extract::Consistency_v::READ_YOUR_WRITES);

    // Element types preserved.
    static_assert(std::is_same_v<
        extract::consistency_value_t<StrongInt>, int>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<CausalDouble>, double>);
    static_assert(std::is_same_v<
        extract::consistency_value_t<RywChar>, char>);

    // Aliases are the SAME TYPE as their expanded form (not just
    // semantically equivalent — the alias declares no new type).
    static_assert(std::is_same_v<StrongInt, C_int_strong>);
    static_assert(std::is_same_v<EventualInt, C_int_eventual>);
}

void test_graded_wrapper_conformance() {
    // Consistency satisfies the GradedWrapper concept (FOUND-G05).
    // This means the FOUND-D09 universal extractors agree with the
    // FOUND-D22 specific extractors:
    //   value_type_of_t<C> == consistency_value_t<C>
    // (D09's predicate works on every GradedWrapper, while D22's
    // works specifically on Consistency<>; they should agree on
    // any Consistency<>.)

    static_assert( extract::is_graded_wrapper_v<C_int_strong>);
    static_assert( extract::is_graded_wrapper_v<C_double_strong>);

    // Universal extractor agrees with specific extractor.
    static_assert(std::is_same_v<
        extract::value_type_of_t<C_int_strong>,
        extract::consistency_value_t<C_int_strong>>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<C_double_strong>,
        extract::consistency_value_t<C_double_strong>>);

    // Bare int is neither wrapper.
    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_consistency_v<int>);
}

void test_cross_wrapper_exclusion() {
    // Consistency vs OwnedRegion vs NumericalTier — fully disjoint.
    static_assert( extract::is_consistency_v<C_int_strong>);
    static_assert(!extract::is_owned_region_v<C_int_strong>);
    static_assert(!extract::is_numerical_tier_v<C_int_strong>);

    static_assert(!extract::is_consistency_v<OR_int_test>);
    static_assert( extract::is_owned_region_v<OR_int_test>);

    static_assert(!extract::is_consistency_v<NT_int_bitexact>);
    static_assert( extract::is_numerical_tier_v<NT_int_bitexact>);

    static_assert(!extract::is_consistency_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_consistency_v<C_int_strong>;
    bool baseline_neg = !extract::is_consistency_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_consistency_v<C_int_strong>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_consistency_v<int>);
        EXPECT_TRUE(extract::IsConsistency<C_int_strong&&>);
        EXPECT_TRUE(!extract::IsConsistency<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_consistency:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_every_level", test_positive_every_level);
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
    run_test("test_level_extraction_every_value",
             test_level_extraction_every_value);
    run_test("test_level_extraction_cv_ref_strips",
             test_level_extraction_cv_ref_strips);
    run_test("test_distinct_levels_distinct_specs",
             test_distinct_levels_distinct_specs);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_consistency",
             test_positive_nested_consistency);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip",
             test_local_alias_round_trip);
    run_test("test_zero_cost_layout_invariant",
             test_zero_cost_layout_invariant);
    run_test("test_public_convenience_aliases",
             test_public_convenience_aliases);
    run_test("test_graded_wrapper_conformance",
             test_graded_wrapper_conformance);
    run_test("test_cross_wrapper_exclusion",
             test_cross_wrapper_exclusion);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
