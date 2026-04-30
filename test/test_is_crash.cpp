// ═══════════════════════════════════════════════════════════════════
// test_is_crash — sentinel TU for safety/IsCrash.h
//
// FOUND-D30 (fourth wrapper of batch).  Mirror of test_is_vendor
// applied to Crash<Class, T>.  4-class (Abort/Throw/ErrorReturn/
// NoThrow) chain-lattice shape.  Pins the chain ordinal invariant
// (Abort=0 ⊥, NoThrow=3 ⊤) so a future inversion or extension breaks
// the build loudly.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsCrash.h>

#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/GradedExtract.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
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

using C_int_abort        = safety::Crash<extract::CrashClass_v::Abort,       int>;
using C_int_throw        = safety::Crash<extract::CrashClass_v::Throw,       int>;
using C_int_error_return = safety::Crash<extract::CrashClass_v::ErrorReturn, int>;
using C_int_no_throw     = safety::Crash<extract::CrashClass_v::NoThrow,     int>;
using C_double_no_throw  = safety::Crash<extract::CrashClass_v::NoThrow,     double>;
using C_float_abort      = safety::Crash<extract::CrashClass_v::Abort,       float>;

struct test_tag {};
using OR_int_test     = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using Cn_int_strong   = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using DS_int_pure     = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using CT_int_hot      = safety::CipherTier<extract::CipherTierTag_v::Hot, int>;
using RH_int_hot      = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, int>;
using V_int_nv        = safety::Vendor<extract::VendorBackend_v::NV, int>;

struct payload_struct { int a; double b; };
using C_struct_no_throw = safety::Crash<
    extract::CrashClass_v::NoThrow, payload_struct>;
using C_nested = safety::Crash<
    extract::CrashClass_v::ErrorReturn, C_int_no_throw>;

}  // namespace

namespace c_test {
void f_takes_no_throw_int(C_int_no_throw const&) noexcept;
void f_takes_abort_int(C_int_abort&&) noexcept;
C_double_no_throw f_returns_no_throw_double(int) noexcept;
}  // namespace c_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_crash_smoke_test());
}

void test_positive_every_class() {
    static_assert( extract::is_crash_v<C_int_abort>);
    static_assert( extract::is_crash_v<C_int_throw>);
    static_assert( extract::is_crash_v<C_int_error_return>);
    static_assert( extract::is_crash_v<C_int_no_throw>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_crash_v<C_double_no_throw>);
    static_assert( extract::is_crash_v<C_float_abort>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_crash_v<C_int_no_throw>);
    static_assert( extract::is_crash_v<C_int_no_throw&>);
    static_assert( extract::is_crash_v<C_int_no_throw&&>);
    static_assert( extract::is_crash_v<C_int_no_throw const&>);
    static_assert( extract::is_crash_v<C_int_no_throw const>);
    static_assert( extract::is_crash_v<C_int_no_throw volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_crash_v<int>);
    static_assert(!extract::is_crash_v<double>);
    static_assert(!extract::is_crash_v<void>);
    static_assert(!extract::is_crash_v<extract::CrashClass_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_crash_v<int*>);
    static_assert(!extract::is_crash_v<C_int_no_throw*>);
    static_assert(!extract::is_crash_v<C_int_no_throw[5]>);
    static_assert(!extract::is_crash_v<C_int_no_throw* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeCrash { int value; extract::CrashClass_v crash_class; };
    static_assert(!extract::is_crash_v<LookalikeCrash>);
}

void test_concept_form_in_constraints() {
    auto callable_with_c = []<typename T>()
        requires extract::IsCrash<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_c.template operator()<C_int_no_throw>());
    EXPECT_TRUE(callable_with_c.template operator()<C_int_abort>());
    EXPECT_TRUE(callable_with_c.template operator()<C_int_throw>());
    EXPECT_TRUE(callable_with_c.template operator()<C_int_error_return>());
    EXPECT_TRUE(callable_with_c.template operator()<C_double_no_throw>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::crash_value_t<C_int_abort>,        int>);
    static_assert(std::is_same_v<extract::crash_value_t<C_int_throw>,        int>);
    static_assert(std::is_same_v<extract::crash_value_t<C_int_error_return>, int>);
    static_assert(std::is_same_v<extract::crash_value_t<C_int_no_throw>,     int>);
    static_assert(std::is_same_v<extract::crash_value_t<C_double_no_throw>,  double>);
    static_assert(std::is_same_v<extract::crash_value_t<C_float_abort>,      float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::crash_value_t<C_int_no_throw const&>, int>);
    static_assert(std::is_same_v<
        extract::crash_value_t<C_int_no_throw&&>, int>);
    static_assert(std::is_same_v<
        extract::crash_value_t<C_double_no_throw const>, double>);
}

void test_class_extraction_every_value() {
    static_assert(extract::crash_class_v<C_int_abort>
                  == extract::CrashClass_v::Abort);
    static_assert(extract::crash_class_v<C_int_throw>
                  == extract::CrashClass_v::Throw);
    static_assert(extract::crash_class_v<C_int_error_return>
                  == extract::CrashClass_v::ErrorReturn);
    static_assert(extract::crash_class_v<C_int_no_throw>
                  == extract::CrashClass_v::NoThrow);
}

void test_class_extraction_cv_ref_strips() {
    static_assert(extract::crash_class_v<C_int_no_throw const&>
                  == extract::CrashClass_v::NoThrow);
    static_assert(extract::crash_class_v<C_int_abort&&>
                  == extract::CrashClass_v::Abort);
}

void test_distinct_classes_distinct_specs() {
    static_assert(std::is_same_v<
        extract::crash_value_t<C_int_no_throw>,
        extract::crash_value_t<C_int_abort>>);
    static_assert(extract::crash_class_v<C_int_no_throw>
                  != extract::crash_class_v<C_int_abort>);

    static_assert(extract::crash_class_v<C_int_no_throw>
                  == extract::crash_class_v<C_double_no_throw>);
    static_assert(!std::is_same_v<
        extract::crash_value_t<C_int_no_throw>,
        extract::crash_value_t<C_double_no_throw>>);
}

void test_chain_ordinal_invariant() {
    // Spec inversion pin — Crash inverts the spec's ordinal hint:
    // bottom = weakest = Abort = 0 ; top = strongest = NoThrow = 3.
    // The detector itself is indifferent to underlying values, but
    // Crucible production code that branches on `<=` over the
    // underlying ordinals (e.g., a Forge phase that demands a
    // minimum strength) depends on this invariant — pin it here.
    static_assert(
        static_cast<std::uint8_t>(extract::CrashClass_v::Abort)       == 0);
    static_assert(
        static_cast<std::uint8_t>(extract::CrashClass_v::Throw)       == 1);
    static_assert(
        static_cast<std::uint8_t>(extract::CrashClass_v::ErrorReturn) == 2);
    static_assert(
        static_cast<std::uint8_t>(extract::CrashClass_v::NoThrow)     == 3);

    // Detector still distinguishes adjacent classes (no value collapse).
    static_assert(extract::crash_class_v<C_int_abort>
                  != extract::crash_class_v<C_int_throw>);
    static_assert(extract::crash_class_v<C_int_throw>
                  != extract::crash_class_v<C_int_error_return>);
    static_assert(extract::crash_class_v<C_int_error_return>
                  != extract::crash_class_v<C_int_no_throw>);
}

void test_dispatcher_integration_example() {
    // Four-arm dispatcher consuming crash_class_v at compile time.
    // Mirrors a Forge phase gate that admits only NoThrow callees on
    // hot paths (returns 3) while routing weaker classes to colder
    // sections (returns 0/1/2 by strength).
    auto select_lowering = []<typename T>() consteval
        requires extract::IsCrash<T>
    {
        constexpr auto c = extract::crash_class_v<T>;
        if constexpr (c == extract::CrashClass_v::Abort)            return 0;
        else if constexpr (c == extract::CrashClass_v::Throw)       return 1;
        else if constexpr (c == extract::CrashClass_v::ErrorReturn) return 2;
        else if constexpr (c == extract::CrashClass_v::NoThrow)     return 3;
        else return -1;  // unreachable
    };

    static_assert(select_lowering.template operator()<C_int_abort>()        == 0);
    static_assert(select_lowering.template operator()<C_int_throw>()        == 1);
    static_assert(select_lowering.template operator()<C_int_error_return>() == 2);
    static_assert(select_lowering.template operator()<C_int_no_throw>()     == 3);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_crash_v<C_struct_no_throw>);
    static_assert(std::is_same_v<
        extract::crash_value_t<C_struct_no_throw>, payload_struct>);
    static_assert(extract::crash_class_v<C_struct_no_throw>
                  == extract::CrashClass_v::NoThrow);
}

void test_positive_nested_crash() {
    static_assert( extract::is_crash_v<C_nested>);
    static_assert(std::is_same_v<
        extract::crash_value_t<C_nested>, C_int_no_throw>);
    static_assert( extract::is_crash_v<extract::crash_value_t<C_nested>>);
    static_assert(std::is_same_v<
        extract::crash_value_t<extract::crash_value_t<C_nested>>, int>);

    // Outer ErrorReturn class distinct from inner NoThrow class.
    static_assert(extract::crash_class_v<C_nested>
                  == extract::CrashClass_v::ErrorReturn);
    static_assert(extract::crash_class_v<
        extract::crash_value_t<C_nested>> == extract::CrashClass_v::NoThrow);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&c_test::f_takes_no_throw_int, 0>;
    static_assert( ex::is_crash_v<P0>);
    static_assert(ex::crash_class_v<P0> == ex::CrashClass_v::NoThrow);
    static_assert(std::is_same_v<ex::crash_value_t<P0>, int>);

    using P0_abort = ex::param_type_t<&c_test::f_takes_abort_int, 0>;
    static_assert( ex::is_crash_v<P0_abort>);
    static_assert(ex::crash_class_v<P0_abort> == ex::CrashClass_v::Abort);

    using R = ex::return_type_t<&c_test::f_returns_no_throw_double>;
    static_assert( ex::is_crash_v<R>);
    static_assert(std::is_same_v<ex::crash_value_t<R>, double>);
    static_assert(ex::crash_class_v<R> == ex::CrashClass_v::NoThrow);
}

void test_local_alias_round_trip() {
    using LocalNoThrowInt = safety::Crash<
        extract::CrashClass_v::NoThrow, int>;

    static_assert( extract::is_crash_v<LocalNoThrowInt>);
    static_assert(extract::crash_class_v<LocalNoThrowInt>
                  == extract::CrashClass_v::NoThrow);
    static_assert(std::is_same_v<LocalNoThrowInt, C_int_no_throw>);
}

void test_zero_cost_layout_invariant() {
    static_assert(sizeof(C_int_abort)        == sizeof(int));
    static_assert(sizeof(C_int_throw)        == sizeof(int));
    static_assert(sizeof(C_int_error_return) == sizeof(int));
    static_assert(sizeof(C_int_no_throw)     == sizeof(int));
    static_assert(sizeof(C_double_no_throw)  == sizeof(double));
    static_assert(sizeof(C_float_abort)      == sizeof(float));
    static_assert(sizeof(C_struct_no_throw)  == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    using AbortInt       = safety::crash::Abort<int>;
    using ThrowInt       = safety::crash::Throw<int>;
    using ErrorReturnInt = safety::crash::ErrorReturn<int>;
    using NoThrowInt     = safety::crash::NoThrow<int>;
    using NoThrowDouble  = safety::crash::NoThrow<double>;

    static_assert( extract::is_crash_v<AbortInt>);
    static_assert( extract::is_crash_v<ThrowInt>);
    static_assert( extract::is_crash_v<ErrorReturnInt>);
    static_assert( extract::is_crash_v<NoThrowInt>);
    static_assert( extract::is_crash_v<NoThrowDouble>);

    static_assert(extract::crash_class_v<AbortInt>
                  == extract::CrashClass_v::Abort);
    static_assert(extract::crash_class_v<ThrowInt>
                  == extract::CrashClass_v::Throw);
    static_assert(extract::crash_class_v<ErrorReturnInt>
                  == extract::CrashClass_v::ErrorReturn);
    static_assert(extract::crash_class_v<NoThrowInt>
                  == extract::CrashClass_v::NoThrow);

    // Round-trip identity: alias → wrapper → detector preserves spec.
    static_assert(std::is_same_v<AbortInt,       C_int_abort>);
    static_assert(std::is_same_v<ThrowInt,       C_int_throw>);
    static_assert(std::is_same_v<ErrorReturnInt, C_int_error_return>);
    static_assert(std::is_same_v<NoThrowInt,     C_int_no_throw>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<C_int_no_throw>);
    static_assert( extract::is_graded_wrapper_v<C_double_no_throw>);
    static_assert( extract::is_graded_wrapper_v<C_int_abort>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<C_int_no_throw>,
        extract::crash_value_t<C_int_no_throw>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_crash_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfC = safety::Consistency<
        extract::Consistency_v::STRONG, C_int_no_throw>;
    using COfConsistency = safety::Crash<
        extract::CrashClass_v::NoThrow, Cn_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfC>);
    static_assert(!extract::is_crash_v<ConsistencyOfC>);
    static_assert( extract::is_crash_v<
        extract::consistency_value_t<ConsistencyOfC>>);

    static_assert( extract::is_crash_v<COfConsistency>);
    static_assert(!extract::is_consistency_v<COfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::crash_value_t<COfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // Crash vs all 8 other shipped wrappers — fully disjoint.
    static_assert( extract::is_crash_v<C_int_no_throw>);
    static_assert(!extract::is_owned_region_v<C_int_no_throw>);
    static_assert(!extract::is_numerical_tier_v<C_int_no_throw>);
    static_assert(!extract::is_consistency_v<C_int_no_throw>);
    static_assert(!extract::is_opaque_lifetime_v<C_int_no_throw>);
    static_assert(!extract::is_det_safe_v<C_int_no_throw>);
    static_assert(!extract::is_cipher_tier_v<C_int_no_throw>);
    static_assert(!extract::is_residency_heat_v<C_int_no_throw>);
    static_assert(!extract::is_vendor_v<C_int_no_throw>);

    static_assert(!extract::is_crash_v<OR_int_test>);
    static_assert(!extract::is_crash_v<NT_int_bitexact>);
    static_assert(!extract::is_crash_v<Cn_int_strong>);
    static_assert(!extract::is_crash_v<OL_int_fleet>);
    static_assert(!extract::is_crash_v<DS_int_pure>);
    static_assert(!extract::is_crash_v<CT_int_hot>);
    static_assert(!extract::is_crash_v<RH_int_hot>);
    static_assert(!extract::is_crash_v<V_int_nv>);

    static_assert(!extract::is_crash_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_crash_v<C_int_no_throw>;
    bool baseline_neg = !extract::is_crash_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_crash_v<C_int_no_throw>);
        EXPECT_TRUE(baseline_neg == !extract::is_crash_v<int>);
        EXPECT_TRUE(extract::IsCrash<C_int_abort&&>);
        EXPECT_TRUE(!extract::IsCrash<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_crash:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_every_class", test_positive_every_class);
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
    run_test("test_class_extraction_every_value",
             test_class_extraction_every_value);
    run_test("test_class_extraction_cv_ref_strips",
             test_class_extraction_cv_ref_strips);
    run_test("test_distinct_classes_distinct_specs",
             test_distinct_classes_distinct_specs);
    run_test("test_chain_ordinal_invariant",
             test_chain_ordinal_invariant);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_crash",
             test_positive_nested_crash);
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
