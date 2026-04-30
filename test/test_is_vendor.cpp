// ═══════════════════════════════════════════════════════════════════
// test_is_vendor — sentinel TU for safety/IsVendor.h
//
// FOUND-D30 (third wrapper of batch).  Mirror of test_is_cipher_tier
// applied to Vendor<Backend, T>.  8-backend (None/CPU/NV/AMD/TPU/TRN/
// CER/Portable) shape with non-contiguous underlying ordinals
// (Portable = 255).  Most exhaustive enum-coverage test in the D30
// batch — every backend exercised in: positive detection, value
// extraction, backend extraction, dispatcher integration, public alias
// round-trip.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsVendor.h>

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
#include <crucible/safety/IsResidencyHeat.h>
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

using V_int_none     = safety::Vendor<extract::VendorBackend_v::None,     int>;
using V_int_cpu      = safety::Vendor<extract::VendorBackend_v::CPU,      int>;
using V_int_nv       = safety::Vendor<extract::VendorBackend_v::NV,       int>;
using V_int_amd      = safety::Vendor<extract::VendorBackend_v::AMD,      int>;
using V_int_tpu      = safety::Vendor<extract::VendorBackend_v::TPU,      int>;
using V_int_trn      = safety::Vendor<extract::VendorBackend_v::TRN,      int>;
using V_int_cer      = safety::Vendor<extract::VendorBackend_v::CER,      int>;
using V_int_portable = safety::Vendor<extract::VendorBackend_v::Portable, int>;
using V_double_nv    = safety::Vendor<extract::VendorBackend_v::NV,       double>;
using V_float_amd    = safety::Vendor<extract::VendorBackend_v::AMD,      float>;

struct test_tag {};
using OR_int_test     = safety::OwnedRegion<int, test_tag>;
using NT_int_bitexact = safety::NumericalTier<extract::Tolerance::BITEXACT, int>;
using C_int_strong    = safety::Consistency<extract::Consistency_v::STRONG, int>;
using OL_int_fleet    = safety::OpaqueLifetime<extract::Lifetime_v::PER_FLEET, int>;
using DS_int_pure     = safety::DetSafe<extract::DetSafeTier_v::Pure, int>;
using CT_int_hot      = safety::CipherTier<extract::CipherTierTag_v::Hot, int>;
using RH_int_hot      = safety::ResidencyHeat<extract::ResidencyHeatTag_v::Hot, int>;

struct payload_struct { int a; double b; };
using V_struct_nv = safety::Vendor<extract::VendorBackend_v::NV, payload_struct>;
using V_nested = safety::Vendor<
    extract::VendorBackend_v::Portable, V_int_nv>;

}  // namespace

namespace v_test {
void f_takes_nv_int(V_int_nv const&) noexcept;
void f_takes_amd_int(V_int_amd&&) noexcept;
V_double_nv f_returns_nv_double(int) noexcept;
}  // namespace v_test

namespace {

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_vendor_smoke_test());
}

void test_positive_every_backend() {
    static_assert( extract::is_vendor_v<V_int_none>);
    static_assert( extract::is_vendor_v<V_int_cpu>);
    static_assert( extract::is_vendor_v<V_int_nv>);
    static_assert( extract::is_vendor_v<V_int_amd>);
    static_assert( extract::is_vendor_v<V_int_tpu>);
    static_assert( extract::is_vendor_v<V_int_trn>);
    static_assert( extract::is_vendor_v<V_int_cer>);
    static_assert( extract::is_vendor_v<V_int_portable>);
}

void test_positive_distinct_element_types() {
    static_assert( extract::is_vendor_v<V_double_nv>);
    static_assert( extract::is_vendor_v<V_float_amd>);
}

void test_cv_ref_stripping() {
    static_assert( extract::is_vendor_v<V_int_nv>);
    static_assert( extract::is_vendor_v<V_int_nv&>);
    static_assert( extract::is_vendor_v<V_int_nv&&>);
    static_assert( extract::is_vendor_v<V_int_nv const&>);
    static_assert( extract::is_vendor_v<V_int_nv const>);
    static_assert( extract::is_vendor_v<V_int_nv volatile>);
}

void test_negative_bare_types() {
    static_assert(!extract::is_vendor_v<int>);
    static_assert(!extract::is_vendor_v<double>);
    static_assert(!extract::is_vendor_v<void>);
    static_assert(!extract::is_vendor_v<extract::VendorBackend_v>);
}

void test_negative_pointers_and_arrays() {
    static_assert(!extract::is_vendor_v<int*>);
    static_assert(!extract::is_vendor_v<V_int_nv*>);
    static_assert(!extract::is_vendor_v<V_int_nv[5]>);
    static_assert(!extract::is_vendor_v<V_int_nv* const>);
}

void test_negative_lookalike_struct() {
    struct LookalikeVendor { int value; extract::VendorBackend_v backend; };
    static_assert(!extract::is_vendor_v<LookalikeVendor>);
}

void test_concept_form_in_constraints() {
    auto callable_with_v = []<typename T>()
        requires extract::IsVendor<T>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_v.template operator()<V_int_nv>());
    EXPECT_TRUE(callable_with_v.template operator()<V_int_portable>());
    EXPECT_TRUE(callable_with_v.template operator()<V_double_nv>());
    EXPECT_TRUE(callable_with_v.template operator()<V_int_none>());
}

void test_value_type_extraction() {
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_none>,     int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_cpu>,      int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_nv>,       int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_amd>,      int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_tpu>,      int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_trn>,      int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_cer>,      int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_int_portable>, int>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_double_nv>,    double>);
    static_assert(std::is_same_v<extract::vendor_value_t<V_float_amd>,    float>);
}

void test_value_type_extraction_cv_ref_strips() {
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_int_nv const&>, int>);
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_int_nv&&>, int>);
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_double_nv const>, double>);
}

void test_backend_extraction_every_value() {
    static_assert(extract::vendor_backend_v<V_int_none>
                  == extract::VendorBackend_v::None);
    static_assert(extract::vendor_backend_v<V_int_cpu>
                  == extract::VendorBackend_v::CPU);
    static_assert(extract::vendor_backend_v<V_int_nv>
                  == extract::VendorBackend_v::NV);
    static_assert(extract::vendor_backend_v<V_int_amd>
                  == extract::VendorBackend_v::AMD);
    static_assert(extract::vendor_backend_v<V_int_tpu>
                  == extract::VendorBackend_v::TPU);
    static_assert(extract::vendor_backend_v<V_int_trn>
                  == extract::VendorBackend_v::TRN);
    static_assert(extract::vendor_backend_v<V_int_cer>
                  == extract::VendorBackend_v::CER);
    static_assert(extract::vendor_backend_v<V_int_portable>
                  == extract::VendorBackend_v::Portable);
}

void test_backend_extraction_cv_ref_strips() {
    static_assert(extract::vendor_backend_v<V_int_nv const&>
                  == extract::VendorBackend_v::NV);
    static_assert(extract::vendor_backend_v<V_int_portable&&>
                  == extract::VendorBackend_v::Portable);
}

void test_distinct_backends_distinct_specs() {
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_int_nv>,
        extract::vendor_value_t<V_int_amd>>);
    static_assert(extract::vendor_backend_v<V_int_nv>
                  != extract::vendor_backend_v<V_int_amd>);

    static_assert(extract::vendor_backend_v<V_int_nv>
                  == extract::vendor_backend_v<V_double_nv>);
    static_assert(!std::is_same_v<
        extract::vendor_value_t<V_int_nv>,
        extract::vendor_value_t<V_double_nv>>);
}

void test_non_contiguous_underlying_value_invariant() {
    // Portable=255 while CER=6 — non-contiguous spacing.  The detector
    // must NOT silently lose this distinction (e.g., a refactor that
    // accidentally maps backend → to_underlying() and back via int
    // arithmetic would wrap the 255 to a different value).
    static_assert(
        static_cast<std::uint8_t>(extract::VendorBackend_v::Portable) == 255);
    static_assert(
        static_cast<std::uint8_t>(extract::VendorBackend_v::CER) == 6);
    static_assert(extract::vendor_backend_v<V_int_portable>
                  != extract::vendor_backend_v<V_int_cer>);
    static_assert(extract::vendor_backend_v<V_int_portable>
                  != extract::vendor_backend_v<V_int_none>);

    static_assert(
        static_cast<std::uint8_t>(extract::VendorBackend_v::None) == 0);
    static_assert(extract::vendor_backend_v<V_int_none>
                  != extract::vendor_backend_v<V_int_portable>);
}

void test_dispatcher_integration_example() {
    // Eight-arm dispatcher consuming vendor_backend_v at compile time.
    // Returns a distinct integer per backend so the test can pin all 8
    // arms without ambiguity.
    auto select_lowering = []<typename T>() consteval
        requires extract::IsVendor<T>
    {
        constexpr auto b = extract::vendor_backend_v<T>;
        if constexpr (b == extract::VendorBackend_v::None)     return 0;
        else if constexpr (b == extract::VendorBackend_v::CPU)      return 1;
        else if constexpr (b == extract::VendorBackend_v::NV)       return 2;
        else if constexpr (b == extract::VendorBackend_v::AMD)      return 3;
        else if constexpr (b == extract::VendorBackend_v::TPU)      return 4;
        else if constexpr (b == extract::VendorBackend_v::TRN)      return 5;
        else if constexpr (b == extract::VendorBackend_v::CER)      return 6;
        else if constexpr (b == extract::VendorBackend_v::Portable) return 7;
        else return -1;  // unreachable
    };

    static_assert(select_lowering.template operator()<V_int_none>()     == 0);
    static_assert(select_lowering.template operator()<V_int_cpu>()      == 1);
    static_assert(select_lowering.template operator()<V_int_nv>()       == 2);
    static_assert(select_lowering.template operator()<V_int_amd>()      == 3);
    static_assert(select_lowering.template operator()<V_int_tpu>()      == 4);
    static_assert(select_lowering.template operator()<V_int_trn>()      == 5);
    static_assert(select_lowering.template operator()<V_int_cer>()      == 6);
    static_assert(select_lowering.template operator()<V_int_portable>() == 7);
}

void test_positive_non_fundamental_wrapped_type() {
    static_assert( extract::is_vendor_v<V_struct_nv>);
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_struct_nv>, payload_struct>);
    static_assert(extract::vendor_backend_v<V_struct_nv>
                  == extract::VendorBackend_v::NV);
}

void test_positive_nested_vendor() {
    static_assert( extract::is_vendor_v<V_nested>);
    static_assert(std::is_same_v<
        extract::vendor_value_t<V_nested>, V_int_nv>);
    static_assert( extract::is_vendor_v<
        extract::vendor_value_t<V_nested>>);
    static_assert(std::is_same_v<
        extract::vendor_value_t<extract::vendor_value_t<V_nested>>, int>);

    // Outer Portable backend distinct from inner NV backend.
    static_assert(extract::vendor_backend_v<V_nested>
                  == extract::VendorBackend_v::Portable);
    static_assert(extract::vendor_backend_v<
        extract::vendor_value_t<V_nested>> == extract::VendorBackend_v::NV);
}

void test_dispatcher_function_parameter_use_case() {
    namespace ex = ::crucible::safety::extract;

    using P0 = ex::param_type_t<&v_test::f_takes_nv_int, 0>;
    static_assert( ex::is_vendor_v<P0>);
    static_assert(ex::vendor_backend_v<P0> == ex::VendorBackend_v::NV);
    static_assert(std::is_same_v<ex::vendor_value_t<P0>, int>);

    using P0_amd = ex::param_type_t<&v_test::f_takes_amd_int, 0>;
    static_assert( ex::is_vendor_v<P0_amd>);
    static_assert(ex::vendor_backend_v<P0_amd> == ex::VendorBackend_v::AMD);

    using R = ex::return_type_t<&v_test::f_returns_nv_double>;
    static_assert( ex::is_vendor_v<R>);
    static_assert(std::is_same_v<ex::vendor_value_t<R>, double>);
    static_assert(ex::vendor_backend_v<R> == ex::VendorBackend_v::NV);
}

void test_local_alias_round_trip() {
    using LocalNvInt = safety::Vendor<extract::VendorBackend_v::NV, int>;

    static_assert( extract::is_vendor_v<LocalNvInt>);
    static_assert(extract::vendor_backend_v<LocalNvInt>
                  == extract::VendorBackend_v::NV);
    static_assert(std::is_same_v<LocalNvInt, V_int_nv>);
}

void test_zero_cost_layout_invariant() {
    static_assert(sizeof(V_int_none)     == sizeof(int));
    static_assert(sizeof(V_int_cpu)      == sizeof(int));
    static_assert(sizeof(V_int_nv)       == sizeof(int));
    static_assert(sizeof(V_int_amd)      == sizeof(int));
    static_assert(sizeof(V_int_tpu)      == sizeof(int));
    static_assert(sizeof(V_int_trn)      == sizeof(int));
    static_assert(sizeof(V_int_cer)      == sizeof(int));
    static_assert(sizeof(V_int_portable) == sizeof(int));
    static_assert(sizeof(V_double_nv)    == sizeof(double));
    static_assert(sizeof(V_struct_nv)    == sizeof(payload_struct));
}

void test_public_convenience_aliases() {
    using NoneInt    = safety::vendor::None<int>;
    using CpuInt     = safety::vendor::Cpu<int>;
    using NvInt      = safety::vendor::Nv<int>;
    using AmdInt     = safety::vendor::Amd<int>;
    using TpuInt     = safety::vendor::Tpu<int>;
    using TrnInt     = safety::vendor::Trn<int>;
    using CerInt     = safety::vendor::Cer<int>;
    using PortableInt = safety::vendor::Portable<int>;
    using NvDouble   = safety::vendor::Nv<double>;

    static_assert( extract::is_vendor_v<NoneInt>);
    static_assert( extract::is_vendor_v<CpuInt>);
    static_assert( extract::is_vendor_v<NvInt>);
    static_assert( extract::is_vendor_v<AmdInt>);
    static_assert( extract::is_vendor_v<TpuInt>);
    static_assert( extract::is_vendor_v<TrnInt>);
    static_assert( extract::is_vendor_v<CerInt>);
    static_assert( extract::is_vendor_v<PortableInt>);
    static_assert( extract::is_vendor_v<NvDouble>);

    static_assert(extract::vendor_backend_v<NoneInt>
                  == extract::VendorBackend_v::None);
    static_assert(extract::vendor_backend_v<CpuInt>
                  == extract::VendorBackend_v::CPU);
    static_assert(extract::vendor_backend_v<NvInt>
                  == extract::VendorBackend_v::NV);
    static_assert(extract::vendor_backend_v<AmdInt>
                  == extract::VendorBackend_v::AMD);
    static_assert(extract::vendor_backend_v<TpuInt>
                  == extract::VendorBackend_v::TPU);
    static_assert(extract::vendor_backend_v<TrnInt>
                  == extract::VendorBackend_v::TRN);
    static_assert(extract::vendor_backend_v<CerInt>
                  == extract::VendorBackend_v::CER);
    static_assert(extract::vendor_backend_v<PortableInt>
                  == extract::VendorBackend_v::Portable);

    // Round-trip identity: Cpu/Nv/Amd alias spelling resolves to the
    // VendorBackend_v::CPU/NV/AMD partial-spec key — mismatched casing
    // (Cpu alias mapped to the lowercase CPU enum) must not lose
    // identity through the wrapper-detector.
    static_assert(std::is_same_v<NvInt, V_int_nv>);
    static_assert(std::is_same_v<AmdInt, V_int_amd>);
    static_assert(std::is_same_v<CpuInt, V_int_cpu>);
    static_assert(std::is_same_v<PortableInt, V_int_portable>);
}

void test_graded_wrapper_conformance() {
    static_assert( extract::is_graded_wrapper_v<V_int_nv>);
    static_assert( extract::is_graded_wrapper_v<V_double_nv>);
    static_assert( extract::is_graded_wrapper_v<V_int_portable>);

    static_assert(std::is_same_v<
        extract::value_type_of_t<V_int_nv>,
        extract::vendor_value_t<V_int_nv>>);

    static_assert(!extract::is_graded_wrapper_v<int>);
    static_assert(!extract::is_vendor_v<int>);
}

void test_outer_wrapper_identity_dominates_nested() {
    using ConsistencyOfV = safety::Consistency<
        extract::Consistency_v::STRONG, V_int_nv>;
    using VOfConsistency = safety::Vendor<
        extract::VendorBackend_v::NV, C_int_strong>;

    static_assert( extract::is_consistency_v<ConsistencyOfV>);
    static_assert(!extract::is_vendor_v<ConsistencyOfV>);
    static_assert( extract::is_vendor_v<
        extract::consistency_value_t<ConsistencyOfV>>);

    static_assert( extract::is_vendor_v<VOfConsistency>);
    static_assert(!extract::is_consistency_v<VOfConsistency>);
    static_assert( extract::is_consistency_v<
        extract::vendor_value_t<VOfConsistency>>);
}

void test_cross_wrapper_exclusion() {
    // Vendor vs all 7 other shipped wrappers — fully disjoint.
    static_assert( extract::is_vendor_v<V_int_nv>);
    static_assert(!extract::is_owned_region_v<V_int_nv>);
    static_assert(!extract::is_numerical_tier_v<V_int_nv>);
    static_assert(!extract::is_consistency_v<V_int_nv>);
    static_assert(!extract::is_opaque_lifetime_v<V_int_nv>);
    static_assert(!extract::is_det_safe_v<V_int_nv>);
    static_assert(!extract::is_cipher_tier_v<V_int_nv>);
    static_assert(!extract::is_residency_heat_v<V_int_nv>);

    static_assert(!extract::is_vendor_v<OR_int_test>);
    static_assert(!extract::is_vendor_v<NT_int_bitexact>);
    static_assert(!extract::is_vendor_v<C_int_strong>);
    static_assert(!extract::is_vendor_v<OL_int_fleet>);
    static_assert(!extract::is_vendor_v<DS_int_pure>);
    static_assert(!extract::is_vendor_v<CT_int_hot>);
    static_assert(!extract::is_vendor_v<RH_int_hot>);

    static_assert(!extract::is_vendor_v<int>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_vendor_v<V_int_nv>;
    bool baseline_neg = !extract::is_vendor_v<int>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_vendor_v<V_int_nv>);
        EXPECT_TRUE(baseline_neg == !extract::is_vendor_v<int>);
        EXPECT_TRUE(extract::IsVendor<V_int_portable&&>);
        EXPECT_TRUE(!extract::IsVendor<int*>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_vendor:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_positive_every_backend", test_positive_every_backend);
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
    run_test("test_backend_extraction_every_value",
             test_backend_extraction_every_value);
    run_test("test_backend_extraction_cv_ref_strips",
             test_backend_extraction_cv_ref_strips);
    run_test("test_distinct_backends_distinct_specs",
             test_distinct_backends_distinct_specs);
    run_test("test_non_contiguous_underlying_value_invariant",
             test_non_contiguous_underlying_value_invariant);
    run_test("test_dispatcher_integration_example",
             test_dispatcher_integration_example);
    run_test("test_positive_non_fundamental_wrapped_type",
             test_positive_non_fundamental_wrapped_type);
    run_test("test_positive_nested_vendor",
             test_positive_nested_vendor);
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
