// ═══════════════════════════════════════════════════════════════════
// test_is_epoch_versioned — sentinel TU for safety/IsEpochVersioned.h
//
// FOUND-D30 (sixth wrapper of batch — second product wrapper).
//
// Mirror of test_is_budgeted, applied to EpochVersioned<T>.  Same
// product-wrapper shape (single template param T, runtime grade
// pair); same 22-test surface.  Distinguishing axis from Budgeted:
// the runtime grade carries (Epoch, Generation) instead of
// (BitsBudget, PeakBytes) — the detector is indifferent to which
// product-lattice components are used, but the cross-wrapper
// exclusion test pins that EV-int and B-int are DIFFERENT types
// despite identical layout.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsEpochVersioned.h>

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

using EV_int      = safety::EpochVersioned<int>;
using EV_double   = safety::EpochVersioned<double>;
using EV_char     = safety::EpochVersioned<char>;
using EV_uint64   = safety::EpochVersioned<std::uint64_t>;
using EV_float    = safety::EpochVersioned<float>;

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

struct payload_struct { int a; double b; };
using EV_struct = safety::EpochVersioned<payload_struct>;
using EV_nested = safety::EpochVersioned<EV_int>;

}  // namespace

namespace ev_test {
void f_takes_ev_int(EV_int const&) noexcept;
void f_takes_ev_double(EV_double&&) noexcept;
EV_uint64 f_returns_ev_uint64(int) noexcept;
}  // namespace ev_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_epoch_versioned_smoke_test());
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_epoch_versioned_v<EV_int>);
    static_assert( extract::is_epoch_versioned_v<EV_double>);
    static_assert( extract::is_epoch_versioned_v<EV_char>);
    static_assert( extract::is_epoch_versioned_v<EV_uint64>);
    static_assert( extract::is_epoch_versioned_v<EV_float>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_epoch_versioned_v<EV_int>);
    static_assert( extract::is_epoch_versioned_v<EV_int&>);
    static_assert( extract::is_epoch_versioned_v<EV_int&&>);
    static_assert( extract::is_epoch_versioned_v<EV_int const&>);
    static_assert( extract::is_epoch_versioned_v<EV_int const>);
    static_assert( extract::is_epoch_versioned_v<EV_int volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_epoch_versioned_v<int>);
    static_assert(!extract::is_epoch_versioned_v<double>);
    static_assert(!extract::is_epoch_versioned_v<void>);
    static_assert(!extract::is_epoch_versioned_v<safety::Epoch>);
    static_assert(!extract::is_epoch_versioned_v<safety::Generation>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_epoch_versioned_v<int*>);
    static_assert(!extract::is_epoch_versioned_v<EV_int*>);
    static_assert(!extract::is_epoch_versioned_v<EV_int[5]>);
    static_assert(!extract::is_epoch_versioned_v<EV_int* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeEpochVersioned {
        int value;
        std::uint64_t epoch_field;
        std::uint64_t generation_field;
    };
    static_assert(!extract::is_epoch_versioned_v<LookalikeEpochVersioned>);
}

void test_concept_form_in_constraints() {
    auto callable_with_ev = []<typename T>()
        requires extract::IsEpochVersioned<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_ev.template operator()<EV_int>());
    EXPECT_TRUE(callable_with_ev.template operator()<EV_double>());
    EXPECT_TRUE(callable_with_ev.template operator()<EV_uint64>());
    EXPECT_TRUE(callable_with_ev.template operator()<EV_struct>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<EV_int>,    int>);
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<EV_double>, double>);
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<EV_char>,   char>);
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<EV_uint64>, std::uint64_t>);
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<EV_float>,  float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EV_int const&>, int>);
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EV_int&&>, int>);
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EV_double const>, double>);
}

void test_distinct_element_types_distinct_specs() {
    static_assert(!std::is_same_v<EV_int, EV_double>);
    static_assert(!std::is_same_v<
        extract::epoch_versioned_value_t<EV_int>,
        extract::epoch_versioned_value_t<EV_double>>);
    static_assert(!std::is_same_v<EV_int, EV_uint64>);

    using EV_int_alt = safety::EpochVersioned<int>;
    static_assert(std::is_same_v<EV_int, EV_int_alt>);
}

void test_runtime_grade_independence_from_type_identity() {
    // Same regime-4 invariant as Budgeted: two EpochVersioned<int>
    // values with DIFFERENT runtime (epoch, generation) pairs are
    // STILL the same C++ type.  Pin the design intent.
    EV_int ev_genesis{0, safety::Epoch{0},     safety::Generation{0}};
    EV_int ev_late  {0, safety::Epoch{1<<20}, safety::Generation{1<<10}};
    static_assert(std::is_same_v<decltype(ev_genesis), decltype(ev_late)>);

    EXPECT_TRUE(extract::is_epoch_versioned_v<decltype(ev_genesis)>);
    EXPECT_TRUE(extract::is_epoch_versioned_v<decltype(ev_late)>);
}

void test_dispatcher_value_type_use_case() {
    // Branch on wrapped element type after confirming
    // EpochVersioned-ness.  Mirrors Canopy reshard path that
    // routes scalar versus aggregate state differently.
    auto select_lowering = []<typename T>() consteval
        requires extract::IsEpochVersioned<T>
    {
        using V = extract::epoch_versioned_value_t<T>;
        if constexpr (std::is_arithmetic_v<V>) return 1;
        else if constexpr (std::is_class_v<V>) return 2;
        else                                    return 0;
    };

    static_assert(select_lowering.template operator()<EV_int>()    == 1);
    static_assert(select_lowering.template operator()<EV_double>() == 1);
    static_assert(select_lowering.template operator()<EV_struct>() == 2);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_epoch_versioned_v<EV_struct>);
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EV_struct>, payload_struct>);
}

void test_positive_nested_epoch_versioned() {
    static_assert( extract::is_epoch_versioned_v<EV_nested>);
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EV_nested>, EV_int>);
    static_assert( extract::is_epoch_versioned_v<
        extract::epoch_versioned_value_t<EV_nested>>);
    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<
            extract::epoch_versioned_value_t<EV_nested>>, int>);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&ev_test::f_takes_ev_int, 0>;
    static_assert( ex::is_epoch_versioned_v<P0>);
    static_assert(std::is_same_v<ex::epoch_versioned_value_t<P0>, int>);

    using P0_double = ex::param_type_t<&ev_test::f_takes_ev_double, 0>;
    static_assert( ex::is_epoch_versioned_v<P0_double>);
    static_assert(std::is_same_v<ex::epoch_versioned_value_t<P0_double>, double>);

    using R = ex::return_type_t<&ev_test::f_returns_ev_uint64>;
    static_assert( ex::is_epoch_versioned_v<R>);
    static_assert(std::is_same_v<ex::epoch_versioned_value_t<R>, std::uint64_t>);
}

void test_local_alias_round_trip() {
    using LocalEvInt = safety::EpochVersioned<int>;
    static_assert( extract::is_epoch_versioned_v<LocalEvInt>);
    static_assert(std::is_same_v<LocalEvInt, EV_int>);
    static_assert(std::is_same_v<extract::epoch_versioned_value_t<LocalEvInt>, int>);
}

void test_runtime_grade_layout_invariant() {
    // Regime-4 layout — same as Budgeted (16-byte runtime grade pair).
    static_assert(sizeof(EV_int)    >= sizeof(int)    + 16);
    static_assert(sizeof(EV_double) >= sizeof(double) + 16);
    static_assert(sizeof(EV_char)   >= sizeof(char)   + 16);
    static_assert(sizeof(EV_uint64) == 24);
    static_assert(sizeof(EV_struct) >= sizeof(payload_struct) + 16);

    // Contrast: NTTP wrapper achieves sizeof == sizeof(T).
    static_assert(sizeof(EV_int) > sizeof(int));
    static_assert(sizeof(V_int_nv) == sizeof(int));
}

void test_distinct_product_wrapper_identity() {
    // EpochVersioned<int> and Budgeted<int> have IDENTICAL layout
    // (both regime-4, both 16-byte runtime grade pair, same wrapped
    // element).  But they are DIFFERENT C++ types — wrapper identity
    // is the partial-spec key, not the layout.  Pin the cross-product-
    // wrapper distinction so a future "unify the product wrappers"
    // refactor (e.g., merging into a single ProductWrapped<L, T>)
    // breaks the build loudly.
    static_assert(!std::is_same_v<EV_int, B_int>);
    static_assert(sizeof(EV_int) == sizeof(B_int));  // identical layout
    static_assert( extract::is_epoch_versioned_v<EV_int>);
    static_assert(!extract::is_epoch_versioned_v<B_int>);
    static_assert( extract::is_budgeted_v<B_int>);
    static_assert(!extract::is_budgeted_v<EV_int>);
}

void test_public_aliases_round_trip() {
    using EpochVersionedInt    = safety::EpochVersioned<int>;
    using EpochVersionedDouble = safety::EpochVersioned<double>;

    static_assert( extract::is_epoch_versioned_v<EpochVersionedInt>);
    static_assert( extract::is_epoch_versioned_v<EpochVersionedDouble>);
    static_assert(std::is_same_v<EpochVersionedInt,    EV_int>);
    static_assert(std::is_same_v<EpochVersionedDouble, EV_double>);

    static_assert(std::is_same_v<
        extract::epoch_versioned_value_t<EpochVersionedInt>, int>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<EV_int>);
    static_assert( extract::is_graded_wrapper_v<EV_double>);
    static_assert( extract::is_graded_wrapper_v<EV_uint64>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<EV_int>,
        extract::epoch_versioned_value_t<EV_int>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_epoch_versioned_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfEv = safety::Consistency<
        extract::Consistency_v::STRONG, EV_int>;
    using EvOfConsistency = safety::EpochVersioned<Cn_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfEv>);
    static_assert(!extract::is_epoch_versioned_v<ConsistencyOfEv>);
    static_assert( extract::is_epoch_versioned_v<
        extract::consistency_value_t<ConsistencyOfEv>>);

    static_assert( extract::is_epoch_versioned_v<EvOfConsistency>);
    static_assert(!extract::is_consistency_v<EvOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::epoch_versioned_value_t<EvOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // EpochVersioned vs all 10 other shipped wrappers — fully disjoint.
    static_assert( extract::is_epoch_versioned_v<EV_int>);
    static_assert(!extract::is_owned_region_v<EV_int>);
    static_assert(!extract::is_numerical_tier_v<EV_int>);
    static_assert(!extract::is_consistency_v<EV_int>);
    static_assert(!extract::is_opaque_lifetime_v<EV_int>);
    static_assert(!extract::is_det_safe_v<EV_int>);
    static_assert(!extract::is_cipher_tier_v<EV_int>);
    static_assert(!extract::is_residency_heat_v<EV_int>);
    static_assert(!extract::is_vendor_v<EV_int>);
    static_assert(!extract::is_crash_v<EV_int>);
    static_assert(!extract::is_budgeted_v<EV_int>);

    static_assert(!extract::is_epoch_versioned_v<OR_int_test>);
    static_assert(!extract::is_epoch_versioned_v<NT_int_bitexact>);
    static_assert(!extract::is_epoch_versioned_v<Cn_int_strong>);
    static_assert(!extract::is_epoch_versioned_v<OL_int_fleet>);
    static_assert(!extract::is_epoch_versioned_v<DS_int_pure>);
    static_assert(!extract::is_epoch_versioned_v<CT_int_hot>);
    static_assert(!extract::is_epoch_versioned_v<RH_int_hot>);
    static_assert(!extract::is_epoch_versioned_v<V_int_nv>);
    static_assert(!extract::is_epoch_versioned_v<C_int_no_throw>);
    static_assert(!extract::is_epoch_versioned_v<B_int>);

    static_assert(!extract::is_epoch_versioned_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_epoch_versioned_v<EV_int>;
    bool baseline_neg = !extract::is_epoch_versioned_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_epoch_versioned_v<EV_int>);
        EXPECT_TRUE(baseline_neg == !extract::is_epoch_versioned_v<int>);
        EXPECT_TRUE(extract::IsEpochVersioned<EV_double&&>);
        EXPECT_TRUE(!extract::IsEpochVersioned<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_epoch_versioned:\n");
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
    run_test("test_positive_nested_epoch_versioned",
             test_positive_nested_epoch_versioned);
    run_test("test_dispatcher_function_parameter_use_case",
             test_dispatcher_function_parameter_use_case);
    run_test("test_local_alias_round_trip", test_local_alias_round_trip);
    run_test("test_runtime_grade_layout_invariant",
             test_runtime_grade_layout_invariant);
    run_test("test_distinct_product_wrapper_identity",
             test_distinct_product_wrapper_identity);
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
