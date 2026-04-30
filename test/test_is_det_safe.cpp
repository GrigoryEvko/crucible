// ═══════════════════════════════════════════════════════════════════
// test_is_det_safe — sentinel TU for safety/IsDetSafe.h
//
// FOUND-D24: fourth wrapper-detector.  Mechanical extension of D23
// (and D21/D22) — the audit-pass template is now stable.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsDetSafe.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
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

// All 7 DetSafeTier values.
using DS_int_pure         = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using DS_int_philox       = safety::DetSafe<extract::DetSafeTier_v::PhiloxRng, int>;
using DS_int_monoclock    = safety::DetSafe<extract::DetSafeTier_v::MonotonicClockRead, int>;
using DS_int_wallclock    = safety::DetSafe<extract::DetSafeTier_v::WallClockRead, int>;
using DS_int_entropy      = safety::DetSafe<extract::DetSafeTier_v::EntropyRead, int>;
using DS_int_fsmtime      = safety::DetSafe<extract::DetSafeTier_v::FilesystemMtime, int>;
using DS_int_nds          = safety::DetSafe<extract::DetSafeTier_v::NonDeterministicSyscall, int>;

using DS_double_pure      = safety::DetSafe<extract::DetSafeTier_v::Pure, double>;
using DS_float_philox     = safety::DetSafe<extract::DetSafeTier_v::PhiloxRng, float>;

// Cross-wrapper exclusion witnesses.
struct test_tag {};
using OR_int_test     = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using C_int_strong    = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;

// Non-fundamental and nested.
struct payload_struct { int a; double b; };
using DS_struct_pure = safety::DetSafe<extract::DetSafeTier_v::Pure, payload_struct>;
using DS_nested = safety::DetSafe<extract::DetSafeTier_v::PhiloxRng, DS_int_pure>;

}  // namespace

namespace ds_test {
void f_takes_pure_int(DS_int_pure const&) noexcept;
void f_takes_nds_int(DS_int_nds&&) noexcept;
DS_double_pure f_returns_pure_double(int) noexcept;
}  // namespace ds_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_det_safe_smoke_test());
}

void test_positive_every_tier() {
    static_assert( extract::is_det_safe_v<DS_int_pure>);
    static_assert( extract::is_det_safe_v<DS_int_philox>);
    static_assert( extract::is_det_safe_v<DS_int_monoclock>);
    static_assert( extract::is_det_safe_v<DS_int_wallclock>);
    static_assert( extract::is_det_safe_v<DS_int_entropy>);
    static_assert( extract::is_det_safe_v<DS_int_fsmtime>);
    static_assert( extract::is_det_safe_v<DS_int_nds>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_det_safe_v<DS_double_pure>);
    static_assert( extract::is_det_safe_v<DS_float_philox>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_det_safe_v<DS_int_pure>);
    static_assert( extract::is_det_safe_v<DS_int_pure&>);
    static_assert( extract::is_det_safe_v<DS_int_pure&&>);
    static_assert( extract::is_det_safe_v<DS_int_pure const&>);
    static_assert( extract::is_det_safe_v<DS_int_pure const>);
    static_assert( extract::is_det_safe_v<DS_int_pure volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_det_safe_v<int>);
    static_assert(!extract::is_det_safe_v<double>);
    static_assert(!extract::is_det_safe_v<void>);
    static_assert(!extract::is_det_safe_v<extract::DetSafeTier_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_det_safe_v<int*>);
    static_assert(!extract::is_det_safe_v<DS_int_pure*>);
    static_assert(!extract::is_det_safe_v<DS_int_pure[5]>);
    static_assert(!extract::is_det_safe_v<DS_int_pure* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeDetSafe { int value; extract::DetSafeTier_v tier; };
    static_assert(!extract::is_det_safe_v<LookalikeDetSafe>);
}

void test_concept_form_in_constraints() {
    auto callable_with_ds = []<typename T>()
        requires extract::IsDetSafe<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_ds.template operator()<DS_int_pure>());
    EXPECT_TRUE(callable_with_ds.template operator()<DS_int_nds>());
    EXPECT_TRUE(callable_with_ds.template operator()<DS_double_pure>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_int_pure>, int>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_int_nds>, int>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_double_pure>, double>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_float_philox>, float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_int_pure const&>, int>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_int_pure&&>, int>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_double_pure const>, double>);
}

void test_tier_extraction_every_value() {
    // All 7 DetSafeTier_v values round-trip.
    static_assert(extract::det_safe_tier_v<DS_int_pure>
                  == extract::DetSafeTier_v::Pure);
    static_assert(extract::det_safe_tier_v<DS_int_philox>
                  == extract::DetSafeTier_v::PhiloxRng);
    static_assert(extract::det_safe_tier_v<DS_int_monoclock>
                  == extract::DetSafeTier_v::MonotonicClockRead);
    static_assert(extract::det_safe_tier_v<DS_int_wallclock>
                  == extract::DetSafeTier_v::WallClockRead);
    static_assert(extract::det_safe_tier_v<DS_int_entropy>
                  == extract::DetSafeTier_v::EntropyRead);
    static_assert(extract::det_safe_tier_v<DS_int_fsmtime>
                  == extract::DetSafeTier_v::FilesystemMtime);
    static_assert(extract::det_safe_tier_v<DS_int_nds>
                  == extract::DetSafeTier_v::NonDeterministicSyscall);
}

void test_tier_extraction_cv_ref_strips() {
    static_assert(extract::det_safe_tier_v<DS_int_pure const&>
                  == extract::DetSafeTier_v::Pure);
    static_assert(extract::det_safe_tier_v<DS_int_nds&&>
                  == extract::DetSafeTier_v::NonDeterministicSyscall);
}

void test_distinct_tiers_distinct_specs() {
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_int_pure>,
        extract::det_safe_value_t<DS_int_nds>>);  // both int
    static_assert(extract::det_safe_tier_v<DS_int_pure>
                  != extract::det_safe_tier_v<DS_int_nds>);

    static_assert(extract::det_safe_tier_v<DS_int_pure>
                  == extract::det_safe_tier_v<DS_double_pure>);
    static_assert(!std::is_same_v<
        extract::det_safe_value_t<DS_int_pure>,
        extract::det_safe_value_t<DS_double_pure>>);
}

void test_dispatcher_integration_example() {
    auto select_lowering = []<typename T>() consteval
        requires extract::IsDetSafe<T>
    {
        constexpr auto tier = extract::det_safe_tier_v<T>;
        if constexpr (tier == extract::DetSafeTier_v::Pure) {
            return 7;  // strictest determinism
        } else if constexpr (tier == extract::DetSafeTier_v::PhiloxRng) {
            return 6;
        } else if constexpr (tier
                             == extract::DetSafeTier_v::MonotonicClockRead) {
            return 5;
        } else if constexpr (tier
                             == extract::DetSafeTier_v::WallClockRead) {
            return 4;
        } else if constexpr (tier == extract::DetSafeTier_v::EntropyRead) {
            return 3;
        } else if constexpr (tier
                             == extract::DetSafeTier_v::FilesystemMtime) {
            return 2;
        } else {
            return 1;  // NonDeterministicSyscall — most permissive
        }
    };

    static_assert(select_lowering.template operator()<DS_int_pure>()      == 7);
    static_assert(select_lowering.template operator()<DS_int_philox>()    == 6);
    static_assert(select_lowering.template operator()<DS_int_monoclock>() == 5);
    static_assert(select_lowering.template operator()<DS_int_wallclock>() == 4);
    static_assert(select_lowering.template operator()<DS_int_entropy>()   == 3);
    static_assert(select_lowering.template operator()<DS_int_fsmtime>()   == 2);
    static_assert(select_lowering.template operator()<DS_int_nds>()       == 1);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_det_safe_v<DS_struct_pure>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_struct_pure>, payload_struct>);
    static_assert(extract::det_safe_tier_v<DS_struct_pure>
                  == extract::DetSafeTier_v::Pure);
}

void test_positive_nested_det_safe() {
    static_assert( extract::is_det_safe_v<DS_nested>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<DS_nested>, DS_int_pure>);
    static_assert( extract::is_det_safe_v<
        extract::det_safe_value_t<DS_nested>>);
    static_assert(std::is_same_v<
        extract::det_safe_value_t<extract::det_safe_value_t<DS_nested>>,
        int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&ds_test::f_takes_pure_int, 0>;
    static_assert( ex::is_det_safe_v<P0>);
    static_assert(ex::det_safe_tier_v<P0> == ex::DetSafeTier_v::Pure);
    static_assert(std::is_same_v<ex::det_safe_value_t<P0>, int>);

    using P0_nds = ex::param_type_t<&ds_test::f_takes_nds_int, 0>;
    static_assert( ex::is_det_safe_v<P0_nds>);
    static_assert(ex::det_safe_tier_v<P0_nds>
                  == ex::DetSafeTier_v::NonDeterministicSyscall);

    using R = ex::return_type_t<&ds_test::f_returns_pure_double>;
    static_assert( ex::is_det_safe_v<R>);
    static_assert(std::is_same_v<ex::det_safe_value_t<R>, double>);
    static_assert(ex::det_safe_tier_v<R> == ex::DetSafeTier_v::Pure);
}

void test_local_alias_round_trip() {
    using LocalPureInt = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;

    static_assert( extract::is_det_safe_v<LocalPureInt>);
    static_assert(extract::det_safe_tier_v<LocalPureInt>
                  == extract::DetSafeTier_v::Pure);
    static_assert(std::is_same_v<LocalPureInt, DS_int_pure>);
}

void test_zero_cost_layout_invariant() {
    static_assert(sizeof(DS_int_pure)    == sizeof(int));
    static_assert(sizeof(DS_int_nds)     == sizeof(int));
    static_assert(sizeof(DS_double_pure) == sizeof(double));
    static_assert(sizeof(DS_struct_pure) == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    // safety::det_safe::Pure<T> etc. are PUBLIC aliases.
    using PureInt        = safety::det_safe::Pure<int>;
    using PhiloxInt      = safety::det_safe::PhiloxRng<int>;
    using MonoClockInt   = safety::det_safe::MonoClock<int>;
    using WallClockInt   = safety::det_safe::WallClock<int>;
    using EntropyInt     = safety::det_safe::EntropyRead<int>;
    using FsMtimeInt     = safety::det_safe::FsMtime<int>;
    using NDSInt         = safety::det_safe::NDS<int>;

    static_assert( extract::is_det_safe_v<PureInt>);
    static_assert( extract::is_det_safe_v<PhiloxInt>);
    static_assert( extract::is_det_safe_v<MonoClockInt>);
    static_assert( extract::is_det_safe_v<WallClockInt>);
    static_assert( extract::is_det_safe_v<EntropyInt>);
    static_assert( extract::is_det_safe_v<FsMtimeInt>);
    static_assert( extract::is_det_safe_v<NDSInt>);

    static_assert(extract::det_safe_tier_v<PureInt>
                  == extract::DetSafeTier_v::Pure);
    static_assert(extract::det_safe_tier_v<PhiloxInt>
                  == extract::DetSafeTier_v::PhiloxRng);
    static_assert(extract::det_safe_tier_v<NDSInt>
                  == extract::DetSafeTier_v::NonDeterministicSyscall);

    static_assert(std::is_same_v<PureInt, DS_int_pure>);
    static_assert(std::is_same_v<NDSInt, DS_int_nds>);
}

void test_graded_wrapper_conformance() {
    // DetSafe satisfies the GradedWrapper concept (FOUND-G14).
    static_assert( extract::is_graded_wrapper_v<DS_int_pure>);
    static_assert( extract::is_graded_wrapper_v<DS_double_pure>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<DS_int_pure>,
        extract::det_safe_value_t<DS_int_pure>>);
    static_assert(std::is_same_v<
        extract::value_type_of_t<DS_double_pure>,
        extract::det_safe_value_t<DS_double_pure>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_det_safe_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    // Composition discipline: outer wrapper's identity dominates.
    using ConsistencyOfDetSafe = safety::Consistency<
        extract::Consistency_v::STRONG, DS_int_pure>;
    using DetSafeOfConsistency = safety::DetSafe<
        extract::DetSafeTier_v::Pure, C_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfDetSafe>);
    static_assert(!extract::is_det_safe_v<ConsistencyOfDetSafe>);
    static_assert( extract::is_det_safe_v<
        extract::consistency_value_t<ConsistencyOfDetSafe>>);

    static_assert( extract::is_det_safe_v<DetSafeOfConsistency>);
    static_assert(!extract::is_consistency_v<DetSafeOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::det_safe_value_t<DetSafeOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // DetSafe vs all other shipped wrappers — fully disjoint.
    static_assert( extract::is_det_safe_v<DS_int_pure>);
    static_assert(!extract::is_owned_region_v<DS_int_pure>);
    static_assert(!extract::is_numerical_tier_v<DS_int_pure>);
    static_assert(!extract::is_consistency_v<DS_int_pure>);
    static_assert(!extract::is_opaque_lifetime_v<DS_int_pure>);

    static_assert(!extract::is_det_safe_v<OR_int_test>);
    static_assert(!extract::is_det_safe_v<NT_int_bitexact>);
    static_assert(!extract::is_det_safe_v<C_int_strong>);
    static_assert(!extract::is_det_safe_v<OL_int_fleet>);

    static_assert(!extract::is_det_safe_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_det_safe_v<DS_int_pure>;
    bool baseline_neg = !extract::is_det_safe_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_det_safe_v<DS_int_pure>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_det_safe_v<int>);
        EXPECT_TRUE(extract::IsDetSafe<DS_int_pure&&>);
        EXPECT_TRUE(!extract::IsDetSafe<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_det_safe:\n");
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
    run_test("test_positive_nested_det_safe",
             test_positive_nested_det_safe);
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
