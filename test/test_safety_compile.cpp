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
//   If the header ships an inline runtime_smoke_test() or similar,
//   add a run_test() invocation in main(); for headers that are
//   pure type-level (Linear, Refined, Tagged, Secret), include-only
//   is enough.
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
#include <crucible/safety/EpochVersioned.h>
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
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/ResidencyHeat.h>
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
void test_epoch_versioned_compile() {
    ::crucible::safety::detail::epoch_versioned_self_test::runtime_smoke_test();
}
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
void test_refined_compile()         {}
void test_refined_algebra_compile() {
    ::crucible::safety::detail::refined_algebra_self_test::runtime_smoke_test();
}
void test_residency_heat_compile()  {
    ::crucible::safety::detail::residency_heat_self_test::runtime_smoke_test();
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
    run_test("test_epoch_versioned_compile", test_epoch_versioned_compile);
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
    run_test("test_refined_compile",         test_refined_compile);
    run_test("test_refined_algebra_compile", test_refined_algebra_compile);
    run_test("test_residency_heat_compile",  test_residency_heat_compile);
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
