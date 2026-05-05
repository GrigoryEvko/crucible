// ═══════════════════════════════════════════════════════════════════
// test_safety_compile — sentinel TU for the safety/* header tree
//
// Why this exists:
//   Per the feedback_header_only_static_assert_blind_spot memory rule:
//   any foundational header tree shipped with embedded `static_assert`
//   blocks is verified ONLY in TUs that actually include it.  Direct-
//   include grep showed 8 of 15 safety/* headers had ZERO direct test
//   inclusions: ConstantTime, Linear, Machine, Mutation, NotInherited,
//   Pinned, Refined, Secret.  They were transitively included through
//   Safety.h / OwnedRegion.h / Workload.h, but per the algebra/* blind-
//   spot discovery (4 latent bugs surfaced when MIGRATE-2 first dragged
//   the algebra headers into a safety/ test TU), explicit per-header
//   TU coverage under the project's full -Werror matrix matters.
//
//   This sentinel forces every safety/* header through the test
//   target's flags.  Any -Werror=shadow / -Werror=switch-default /
//   display_string_of context-fragility / similar latent issue surfaces
//   here, where it's cheap to fix, instead of inside a future MIGRATE-*
//   commit where it's a noisy distraction.
//
// Coverage discipline:
//   When a new safety/* header ships, add its include below.  The
//   include alone is sufficient — any embedded static_assert blocks
//   fire at TU-include time under the test target's warning matrix.
//   New type-level harness belongs in this sentinel TU or negative
//   compile fixtures unless the assertion is load-bearing at the
//   production template boundary itself.
//
// Trust boundary:
//   Some safety/ headers (OwnedRegion, Workload, Simd) already have
//   dedicated tests with deeper coverage.  This sentinel does NOT
//   duplicate those; it only guarantees the EMBEDDED static_assert
//   blocks in EVERY header fire.  The dedicated tests remain the
//   authoritative behavioral coverage.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/IsBits.h>
#include <crucible/safety/IsBorrowed.h>
#include <crucible/safety/IsBorrowedRef.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Reflected.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Saturated.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Safety.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Workload.h>

#include <cstdio>
#include <cstdlib>

namespace {

namespace cs = crucible::safety;
namespace fn = crucible::safety::fn;

namespace dimension_traits_compile_probe {

static_assert(cs::TIER_KIND_COUNT == 5);
static_assert(cs::DIMENSION_AXIS_COUNT == 20);

[[nodiscard]] consteval bool every_tier_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^cs::TierKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = cs::tier_kind_name([:en:]);
        if (n == std::string_view{"<unknown TierKind>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval bool every_dimension_axis_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^cs::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = cs::dimension_axis_name([:en:]);
        if (n == std::string_view{"<unknown DimensionAxis>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval bool every_dimension_axis_has_tier() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^cs::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cs::tier_kind_name(cs::tier_of_axis([:en:]))
            == std::string_view{"<unknown TierKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval std::size_t count_dims_in_tier(cs::TierKind t) noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^cs::DimensionAxis));
    std::size_t n = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cs::tier_of_axis([:en:]) == t) ++n;
    }
#pragma GCC diagnostic pop
    return n;
}

static_assert(every_tier_kind_has_name());
static_assert(every_dimension_axis_has_name());
static_assert(every_dimension_axis_has_tier());
static_assert(count_dims_in_tier(cs::TierKind::Semiring) == 15);
static_assert(count_dims_in_tier(cs::TierKind::Lattice) == 1);
static_assert(count_dims_in_tier(cs::TierKind::Typestate) == 1);
static_assert(count_dims_in_tier(cs::TierKind::Foundational) == 2);
static_assert(count_dims_in_tier(cs::TierKind::Versioned) == 1);

struct TestLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};

struct TestSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr bool bottom() noexcept { return false; }
    [[nodiscard]] static constexpr bool top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static constexpr bool zero() noexcept { return false; }
    [[nodiscard]] static constexpr bool one() noexcept { return true; }
    [[nodiscard]] static constexpr bool add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool mul(bool a, bool b) noexcept { return a && b; }
};

struct TestVersioned {
    using element_type = std::uint32_t;
    [[nodiscard]] static constexpr bool compatible(std::uint32_t a,
                                                   std::uint32_t b) noexcept {
        return a == b;
    }
};

struct TestTypestate {
    using state_type = int;
    using transition_type = int;
};

struct TestBareFoundational {
    int payload{0};
};

static_assert(cs::LatticeGrade<TestLattice>);
static_assert(!cs::SemiringGrade<TestLattice>);
static_assert(cs::LatticeGrade<TestSemiring>);
static_assert(cs::SemiringGrade<TestSemiring>);
static_assert(cs::VersionedGrade<TestVersioned>);
static_assert(!cs::LatticeGrade<TestVersioned>);
static_assert(cs::TypestateGrade<TestTypestate>);
static_assert(!cs::VersionedGrade<TestTypestate>);
static_assert(cs::FoundationalGrade<int>);
static_assert(cs::FoundationalGrade<TestBareFoundational>);
static_assert(cs::tier_for_grade_v<TestSemiring> == cs::TierKind::Semiring);
static_assert(cs::tier_for_grade_v<TestLattice> == cs::TierKind::Lattice);
static_assert(cs::tier_for_grade_v<TestTypestate> == cs::TierKind::Typestate);
static_assert(cs::tier_for_grade_v<TestVersioned> == cs::TierKind::Versioned);
static_assert(cs::tier_for_grade_v<TestBareFoundational> == cs::TierKind::Foundational);

struct QuadTag {};
using WLinear = cs::Linear<int>;
using WRefined = cs::Refined<cs::positive, int>;
using WSealedRefined = cs::SealedRefined<cs::positive, int>;
using WTagged = cs::Tagged<int, cs::source::FromUser>;
using WSecret = cs::Secret<int>;
using WStale = cs::Stale<int>;
using WTimeOrdered = cs::TimeOrdered<int, 4, QuadTag>;
using WMonotonic = cs::Monotonic<std::uint64_t>;
using WAppendOnly = cs::AppendOnly<int>;
using WHotPath = cs::HotPath<cs::HotPathTier_v::Hot, int>;
using WDetSafe = cs::DetSafe<cs::DetSafeTier_v::Pure, int>;
using WNumericalTier = cs::NumericalTier<cs::Tolerance::BITEXACT, int>;
using WVendor = cs::Vendor<cs::VendorBackend_v::Portable, int>;
using WResidencyHeat = cs::ResidencyHeat<cs::ResidencyHeatTag_v::Hot, int>;
using WCipherTier = cs::CipherTier<cs::CipherTierTag_v::Hot, int>;
using WAllocClass = cs::AllocClass<cs::AllocClassTag_v::Arena, int>;
using WWait = cs::Wait<cs::WaitStrategy_v::SpinPause, int>;
using WMemOrder = cs::MemOrder<cs::MemOrderTag_v::SeqCst, int>;
using WProgress = cs::Progress<cs::ProgressClass_v::Bounded, int>;
using WConsistency = cs::Consistency<cs::Consistency_v::STRONG, int>;
using WOpaqueLifetime = cs::OpaqueLifetime<cs::Lifetime_v::PER_REQUEST, int>;
using WCrash = cs::Crash<cs::CrashClass_v::NoThrow, int>;
using WBudgeted = cs::Budgeted<int>;
using WEpochVersioned = cs::EpochVersioned<int>;
using WNumaPlacement = cs::NumaPlacement<int>;
using WRecipeSpec = cs::RecipeSpec<int>;

static_assert(cs::wrapper_tier_v<WLinear> == cs::TierKind::Semiring);
static_assert(cs::wrapper_tier_v<WRefined> == cs::TierKind::Foundational);
static_assert(cs::wrapper_tier_v<WTagged> == cs::TierKind::Semiring);
static_assert(cs::wrapper_tier_v<WSecret> == cs::TierKind::Semiring);
static_assert(cs::wrapper_tier_v<WTimeOrdered> == cs::TierKind::Lattice);
static_assert(cs::wrapper_tier_v<WEpochVersioned> == cs::TierKind::Versioned);
static_assert(cs::verify_quadruple<WLinear>());
static_assert(cs::verify_quadruple<WRefined>());
static_assert(cs::verify_quadruple<WSealedRefined>());
static_assert(cs::verify_quadruple<WTagged>());
static_assert(cs::verify_quadruple<WSecret>());
static_assert(cs::verify_quadruple<WStale>());
static_assert(cs::verify_quadruple<WTimeOrdered>());
static_assert(cs::verify_quadruple<WMonotonic>());
static_assert(cs::verify_quadruple<WAppendOnly>());
static_assert(cs::verify_quadruple<WHotPath>());
static_assert(cs::verify_quadruple<WDetSafe>());
static_assert(cs::verify_quadruple<WNumericalTier>());
static_assert(cs::verify_quadruple<WVendor>());
static_assert(cs::verify_quadruple<WResidencyHeat>());
static_assert(cs::verify_quadruple<WCipherTier>());
static_assert(cs::verify_quadruple<WAllocClass>());
static_assert(cs::verify_quadruple<WWait>());
static_assert(cs::verify_quadruple<WMemOrder>());
static_assert(cs::verify_quadruple<WProgress>());
static_assert(cs::verify_quadruple<WConsistency>());
static_assert(cs::verify_quadruple<WOpaqueLifetime>());
static_assert(cs::verify_quadruple<WCrash>());
static_assert(cs::verify_quadruple<WBudgeted>());
static_assert(cs::verify_quadruple<WEpochVersioned>());
static_assert(cs::verify_quadruple<WNumaPlacement>());
static_assert(cs::verify_quadruple<WRecipeSpec>());

}  // namespace dimension_traits_compile_probe

namespace fn_compile_probe {

using DefaultFn = fn::Fn<int>;
using CustomFn = fn::Fn<float, fn::pred::True, fn::UsageMode::Affine,
                        crucible::effects::Row<>, fn::SecLevel::Public,
                        fn::proto::None, fn::lifetime::Static,
                        fn::source::FromUser, fn::trust::Tested,
                        fn::ReprKind::C, fn::cost::Constant,
                        fn::precision::F32, fn::space::Zero,
                        fn::OverflowMode::Saturate, fn::MutationMode::Mutable,
                        fn::ReentrancyMode::Reentrant, fn::size_pol::Unstated,
                        3, fn::stale::Fresh>;

static_assert(sizeof(DefaultFn) == sizeof(int));
static_assert(sizeof(fn::Fn<char>) == sizeof(char));
static_assert(sizeof(fn::Fn<double>) == sizeof(double));
static_assert(sizeof(fn::Fn<int, fn::pred::True, fn::UsageMode::Affine,
                            crucible::effects::Row<>,
                            fn::SecLevel::Public>) == sizeof(int));
static_assert(std::is_same_v<DefaultFn::type_t, int>);
static_assert(DefaultFn::usage_v == fn::UsageMode::Linear);
static_assert(std::is_same_v<DefaultFn::effect_row_t, crucible::effects::Row<>>);
static_assert(DefaultFn::security_v == fn::SecLevel::Classified);
static_assert(DefaultFn::repr_v == fn::ReprKind::Opaque);
static_assert(DefaultFn::overflow_v == fn::OverflowMode::Trap);
static_assert(DefaultFn::mutation_v == fn::MutationMode::Immutable);
static_assert(DefaultFn::reentrancy_v == fn::ReentrancyMode::NonReentrant);
static_assert(DefaultFn::version_v == 1);
static_assert(CustomFn::usage_v == fn::UsageMode::Affine);
static_assert(CustomFn::security_v == fn::SecLevel::Public);
static_assert(std::is_same_v<CustomFn::source_t, fn::source::FromUser>);
static_assert(std::is_same_v<CustomFn::trust_t, fn::trust::Tested>);
static_assert(CustomFn::repr_v == fn::ReprKind::C);
static_assert(CustomFn::overflow_v == fn::OverflowMode::Saturate);
static_assert(CustomFn::mutation_v == fn::MutationMode::Mutable);
static_assert(CustomFn::reentrancy_v == fn::ReentrancyMode::Reentrant);
static_assert(CustomFn::version_v == 3);
static_assert(fn::ValidComposition<DefaultFn>);
static_assert(fn::ValidComposition<CustomFn>);
static_assert(fn::mint_fn(42).value_ == 42);
static_assert(std::is_same_v<decltype(fn::mint_fn(42)), fn::Fn<int>>);
static_assert(std::is_same_v<decltype(fn::mint_fn(3.14)), fn::Fn<double>>);

[[nodiscard]] consteval std::size_t usage_mode_count() noexcept {
    return std::meta::enumerators_of(^^fn::UsageMode).size();
}
[[nodiscard]] consteval std::size_t sec_level_count() noexcept {
    return std::meta::enumerators_of(^^fn::SecLevel).size();
}
[[nodiscard]] consteval std::size_t repr_kind_count() noexcept {
    return std::meta::enumerators_of(^^fn::ReprKind).size();
}
[[nodiscard]] consteval std::size_t overflow_mode_count() noexcept {
    return std::meta::enumerators_of(^^fn::OverflowMode).size();
}
[[nodiscard]] consteval std::size_t mutation_mode_count() noexcept {
    return std::meta::enumerators_of(^^fn::MutationMode).size();
}
[[nodiscard]] consteval std::size_t reentrancy_mode_count() noexcept {
    return std::meta::enumerators_of(^^fn::ReentrancyMode).size();
}

static_assert(usage_mode_count() == 6);
static_assert(sec_level_count() == 5);
static_assert(repr_kind_count() == 6);
static_assert(overflow_mode_count() == 4);
static_assert(mutation_mode_count() == 4);
static_assert(reentrancy_mode_count() == 3);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol) == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation) == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version) == cs::TierKind::Versioned);
static_assert(std::is_default_constructible_v<DefaultFn>);
static_assert(std::is_nothrow_default_constructible_v<DefaultFn>);
static_assert(std::is_nothrow_move_constructible_v<DefaultFn>);
static_assert(std::is_nothrow_move_assignable_v<DefaultFn>);
static_assert(std::is_nothrow_copy_constructible_v<DefaultFn>);
static_assert(std::is_trivially_destructible_v<DefaultFn>);

struct AggregateWithConstMember { const int x = 0; int y = 0; };
struct AggregateWithArrayMember { int xs[4]{}; };
struct MoveOnly {
    int v = 0;
    constexpr MoveOnly() noexcept = default;
    explicit constexpr MoveOnly(int x) noexcept : v{x} {}
    constexpr MoveOnly(const MoveOnly&) = delete;
    constexpr MoveOnly& operator=(const MoveOnly&) = delete;
    constexpr MoveOnly(MoveOnly&&) noexcept = default;
    constexpr MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

static_assert(sizeof(fn::Fn<unsigned int>) == sizeof(unsigned int));
static_assert(sizeof(fn::Fn<long long>) == sizeof(long long));
static_assert(sizeof(fn::Fn<AggregateWithConstMember>) == sizeof(AggregateWithConstMember));
static_assert(sizeof(fn::Fn<AggregateWithArrayMember>) == sizeof(AggregateWithArrayMember));
static_assert(sizeof(fn::Fn<const int*>) == sizeof(const int*));
static_assert(std::is_constructible_v<fn::Fn<MoveOnly>, MoveOnly>);
static_assert(std::is_move_constructible_v<fn::Fn<MoveOnly>>);
static_assert(!std::is_copy_constructible_v<fn::Fn<MoveOnly>>);
static_assert(std::is_same_v<decltype(fn::mint_fn(MoveOnly{123})), fn::Fn<MoveOnly>>);

}  // namespace fn_compile_probe

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

// ── Per-header compile probes ───────────────────────────────────────
//
// One probe per header that ships with embedded static_asserts.  The
// probes are body-empty by design: reaching the run_test invocation
// proves the include was processed under the test target's full
// warning matrix without the embedded asserts firing.

void test_alloc_class_compile()     {
    ::crucible::safety::detail::alloc_class_self_test::runtime_smoke_test();
}
void test_bits_compile()            {
    ::crucible::safety::detail::bits_self_test::runtime_smoke_test();
}
void test_borrowed_compile()        {
    ::crucible::safety::detail::borrowed_self_test::runtime_smoke_test();
}
void test_is_bits_compile()         {
    ::crucible::safety::extract::detail::is_bits_self_test::runtime_smoke_test();
}
void test_is_borrowed_compile()     {
    ::crucible::safety::extract::detail::is_borrowed_self_test::runtime_smoke_test();
}
void test_is_borrowed_ref_compile() {
    ::crucible::safety::extract::detail::is_borrowed_ref_self_test::runtime_smoke_test();
}
void test_budgeted_compile()        {
    ::crucible::safety::detail::budgeted_self_test::runtime_smoke_test();
}
void test_checked_compile()         {}
void test_cipher_tier_compile()     {
    ::crucible::safety::detail::cipher_tier_self_test::runtime_smoke_test();
}
void test_constant_time_compile()   {}
void test_consistency_compile()     {
    ::crucible::safety::detail::consistency_self_test::runtime_smoke_test();
}
void test_crash_compile()           {
    ::crucible::safety::detail::crash_self_test::runtime_smoke_test();
}
void test_det_safe_compile()        {
    ::crucible::safety::detail::det_safe_self_test::runtime_smoke_test();
}
void test_dimension_traits_compile() {}
void test_epoch_versioned_compile() {
    ::crucible::safety::detail::epoch_versioned_self_test::runtime_smoke_test();
}
void test_fixed_array_compile()     {
    ::crucible::safety::detail::fixed_array_self_test::runtime_smoke_test();
}
void test_fn_compile()              {}
void test_numa_placement_compile() {
    ::crucible::safety::detail::numa_placement_self_test::runtime_smoke_test();
}
void test_recipe_spec_compile() {
    ::crucible::safety::detail::recipe_spec_self_test::runtime_smoke_test();
}
void test_hot_path_compile()        {
    ::crucible::safety::detail::hot_path_self_test::runtime_smoke_test();
}
void test_linear_compile()          {}
void test_machine_compile()         {}
void test_mem_order_compile()       {
    ::crucible::safety::detail::mem_order_self_test::runtime_smoke_test();
}
void test_mutation_compile()        {}
void test_not_inherited_compile()   {}
void test_numerical_tier_compile()  {
    ::crucible::safety::detail::numerical_tier_self_test::runtime_smoke_test();
}
void test_opaque_lifetime_compile() {
    ::crucible::safety::detail::opaque_lifetime_self_test::runtime_smoke_test();
}
void test_owned_region_compile()    {}
void test_pinned_compile()          {}
void test_progress_compile()        {
    ::crucible::safety::detail::progress_self_test::runtime_smoke_test();
}
void test_reflected_compile()       {
    ::crucible::safety::reflected::detail::reflected_self_test::runtime_smoke_test();
}
void test_refined_compile()         {}
void test_refined_algebra_compile() {
    ::crucible::safety::detail::refined_algebra_self_test::runtime_smoke_test();
}
void test_residency_heat_compile()  {
    ::crucible::safety::detail::residency_heat_self_test::runtime_smoke_test();
}
void test_saturated_compile()       {
    ::crucible::safety::detail::saturated_self_test::runtime_smoke_test();
}
void test_safety_umbrella_compile() {}
void test_vendor_compile()          {
    ::crucible::safety::detail::vendor_self_test::runtime_smoke_test();
}
void test_scoped_view_compile()     {}
void test_sealed_refined_compile()  {}
void test_secret_compile()          {}
void test_simd_compile()            {}
void test_stale_compile()           {
    ::crucible::safety::detail::stale_self_test::runtime_smoke_test();
}
void test_tagged_compile()          {}
void test_time_ordered_compile()    {
    ::crucible::safety::detail::time_ordered_self_test::runtime_smoke_test();
}
void test_wait_compile()            {
    ::crucible::safety::detail::wait_self_test::runtime_smoke_test();
}
void test_workload_compile()        {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_safety_compile:\n");

    run_test("test_alloc_class_compile",     test_alloc_class_compile);
    run_test("test_bits_compile",            test_bits_compile);
    run_test("test_borrowed_compile",        test_borrowed_compile);
    run_test("test_is_bits_compile",         test_is_bits_compile);
    run_test("test_is_borrowed_compile",     test_is_borrowed_compile);
    run_test("test_is_borrowed_ref_compile", test_is_borrowed_ref_compile);
    run_test("test_budgeted_compile",        test_budgeted_compile);
    run_test("test_checked_compile",         test_checked_compile);
    run_test("test_cipher_tier_compile",     test_cipher_tier_compile);
    run_test("test_constant_time_compile",   test_constant_time_compile);
    run_test("test_consistency_compile",     test_consistency_compile);
    run_test("test_crash_compile",           test_crash_compile);
    run_test("test_det_safe_compile",        test_det_safe_compile);
    run_test("test_dimension_traits_compile", test_dimension_traits_compile);
    run_test("test_epoch_versioned_compile", test_epoch_versioned_compile);
    run_test("test_fixed_array_compile",     test_fixed_array_compile);
    run_test("test_fn_compile",              test_fn_compile);
    run_test("test_numa_placement_compile",  test_numa_placement_compile);
    run_test("test_recipe_spec_compile",     test_recipe_spec_compile);
    run_test("test_hot_path_compile",        test_hot_path_compile);
    run_test("test_linear_compile",          test_linear_compile);
    run_test("test_machine_compile",         test_machine_compile);
    run_test("test_mem_order_compile",       test_mem_order_compile);
    run_test("test_mutation_compile",        test_mutation_compile);
    run_test("test_not_inherited_compile",   test_not_inherited_compile);
    run_test("test_numerical_tier_compile",  test_numerical_tier_compile);
    run_test("test_opaque_lifetime_compile", test_opaque_lifetime_compile);
    run_test("test_owned_region_compile",    test_owned_region_compile);
    run_test("test_pinned_compile",          test_pinned_compile);
    run_test("test_progress_compile",        test_progress_compile);
    run_test("test_reflected_compile",       test_reflected_compile);
    run_test("test_refined_compile",         test_refined_compile);
    run_test("test_refined_algebra_compile", test_refined_algebra_compile);
    run_test("test_residency_heat_compile",  test_residency_heat_compile);
    run_test("test_saturated_compile",       test_saturated_compile);
    run_test("test_safety_umbrella_compile", test_safety_umbrella_compile);
    run_test("test_scoped_view_compile",     test_scoped_view_compile);
    run_test("test_sealed_refined_compile",  test_sealed_refined_compile);
    run_test("test_secret_compile",          test_secret_compile);
    run_test("test_simd_compile",            test_simd_compile);
    run_test("test_stale_compile",           test_stale_compile);
    run_test("test_tagged_compile",          test_tagged_compile);
    run_test("test_time_ordered_compile",    test_time_ordered_compile);
    run_test("test_vendor_compile",          test_vendor_compile);
    run_test("test_wait_compile",            test_wait_compile);
    run_test("test_workload_compile",        test_workload_compile);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
