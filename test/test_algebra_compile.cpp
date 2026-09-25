// These headers carry their verification inside themselves, as
// assertions and inline smoke tests.  A header no translation unit
// includes is never compiled under the project warning flags, and its
// assertions never run at all.  This file is where each of them is
// included and each of their smoke tests is called.
//
// A new header here needs both: the include and its own call in the
// list at the bottom.  A new lattice header also needs its lattice in
// the name-coverage pack below.  No check finds a lattice missing there.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/_Modality.h>
#include <crucible/algebra/lattices/_AffinityLattice.h>
#include <crucible/algebra/lattices/_AllocClassLattice.h>
#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/_BitsBudgetLattice.h>
#include <crucible/algebra/lattices/_BoolLattice.h>
#include <crucible/algebra/lattices/_CallShapeLattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>
#include <crucible/algebra/lattices/_CipherTierLattice.h>
#include <crucible/algebra/lattices/_ClockSourceLattice.h>
#include <crucible/algebra/lattices/_ConfLattice.h>
#include <crucible/algebra/lattices/_ConsistencyLattice.h>
#include <crucible/algebra/lattices/_ControlFlowLattice.h>
#include <crucible/algebra/lattices/_CrashLattice.h>
#include <crucible/algebra/lattices/_DetSafeLattice.h>
#include <crucible/algebra/lattices/_EnumValuePins.h>
#include <crucible/algebra/lattices/_EpochLattice.h>
#include <crucible/algebra/lattices/_FpModeLattice.h>
#include <crucible/algebra/lattices/_FractionalLattice.h>
#include <crucible/algebra/lattices/_GenerationLattice.h>
#include <crucible/algebra/lattices/_GlobalStateLattice.h>
#include <crucible/algebra/lattices/_HappensBefore.h>
#include <crucible/algebra/lattices/_HotPathLattice.h>
#include <crucible/algebra/lattices/_HwInstructionLattice.h>
#include <crucible/algebra/lattices/_JoinPolicyLattice.h>
#include <crucible/algebra/lattices/_LifetimeLattice.h>
#include <crucible/algebra/lattices/_MemOrderLattice.h>
#include <crucible/algebra/lattices/_MonotoneLattice.h>
#include <crucible/algebra/lattices/_NumaNodeLattice.h>
#include <crucible/algebra/lattices/_PeakBytesLattice.h>
#include <crucible/algebra/lattices/_PinningRequirementLattice.h>
#include <crucible/algebra/lattices/_ProductLattice.h>
#include <crucible/algebra/lattices/_ProgressLattice.h>
#include <crucible/algebra/lattices/_QttSemiring.h>
#include <crucible/algebra/lattices/_RecipeFamilyLattice.h>
#include <crucible/algebra/lattices/_ResidencyHeatLattice.h>
#include <crucible/algebra/lattices/_SchedulerPolicyLattice.h>
#include <crucible/algebra/lattices/_SeqPrefixLattice.h>
#include <crucible/algebra/lattices/_SimdIsaLattice.h>
#include <crucible/algebra/lattices/_StackUseLattice.h>
#include <crucible/algebra/lattices/_StalenessSemiring.h>
#include <crucible/algebra/lattices/_StdioLattice.h>
#include <crucible/algebra/lattices/_SuspendBehaviorLattice.h>
#include <crucible/algebra/lattices/_SyscallFamilyLattice.h>
#include <crucible/algebra/lattices/_ToleranceLattice.h>
#include <crucible/algebra/lattices/_TrustLattice.h>
#include <crucible/algebra/lattices/_VendorLattice.h>
#include <crucible/algebra/lattices/_WaitLattice.h>
#include <crucible/algebra/lattices/_WitnessLattice.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace crucible::algebra::lattices::detail::lattice_name_coverage {

// A lattice with no name() member renders as an "<unnamed lattice>"
// sentinel in every diagnostic that mentions it, which no test sees.
// The pack below is the guard, and it is maintained by hand: adding a
// lattice header to the includes above without adding the lattice here
// does not fire.  The pack is the grep point for that discipline.
//
// A templated lattice's name() does not depend on its arguments, so one
// representative instantiation per template is enough to catch a
// removed member.

// Stateless tags that exist only to fill a template parameter.  No
// lattice operation is exercised here.
struct LatticeNameProbeTruePred {
    template <typename T>
    [[nodiscard]] static constexpr bool check(T const&) noexcept {
        return true;
    }
};
struct LatticeNameProbeSource {};
struct LatticeNameProbeElement {
    std::uint32_t v = 0;
    [[nodiscard]] constexpr bool operator==(LatticeNameProbeElement const&) const noexcept = default;
};

template <typename... Ls>
[[nodiscard]] consteval bool every_lattice_has_name() noexcept {
    return (HasLatticeName<Ls> && ...);
}

static_assert(
    every_lattice_has_name<
        AffinityLattice, AllocClassLattice, BarrierStrengthLattice, BitsBudgetLattice,
        BoolLattice<LatticeNameProbeTruePred>, CallShapeLattice, CipherTierLattice, ClockSourceLattice, ConfLattice,
        ConsistencyLattice, ControlFlowLattice, CrashLattice, DetSafeLattice, EpochLattice, FpComplexLayoutLattice,
        FpConstantRoundingLattice, FpContractLattice, FpDenormalInputLattice, FpFtzLattice, FpInfPolicyLattice,
        FpLibmPolicyLattice, FpNanPolicyLattice, FpReassociateLattice, FpRoundingLattice, FpTrapMaskLattice,
        FractionalLattice, GenerationLattice, GlobalStateLattice, HappensBeforeLattice<4>, HotPathLattice,
        HwInstructionLattice, LifetimeLattice, MemOrderLattice, MonotoneLattice<int, std::less<int>>, NumaNodeLattice,
        PeakBytesLattice, PinningRequirementLattice, ProductLattice<HotPathLattice, DetSafeLattice>,
        // The binary product above already covers the template
        // structurally.  Pinning the eleven-way form as well guards
        // against an arity refactor slipping past this pack.
        ProductLattice<FpRoundingLattice, FpFtzLattice, FpContractLattice, FpTrapMaskLattice, FpDenormalInputLattice,
                       FpNanPolicyLattice, FpInfPolicyLattice, FpComplexLayoutLattice, FpLibmPolicyLattice,
                       FpReassociateLattice, FpConstantRoundingLattice>,
        ProgressLattice, QttSemiring, RecipeFamilyLattice, ResidencyHeatLattice, SchedulerPolicyLattice,
        SeqPrefixLattice<LatticeNameProbeElement>, SimdIsaLattice, StackUseLattice, StalenessSemiring, StdioLattice,
        SuspendBehaviorLattice, SyscallFamilyLattice, ToleranceLattice, TrustLattice<LatticeNameProbeSource>,
        VendorLattice, WaitLattice, WitnessLattice>(),
    "[Lattice_Missing_Name] At least one shipped lattice does not "
    "satisfy HasLatticeName<L>, so `lattice_name<L>()` returns the "
    "`<unnamed lattice>` sentinel and every diagnostic naming that "
    "lattice degrades silently.  Add `static consteval "
    "std::string_view name() noexcept` to the lattice.");

}  // namespace crucible::algebra::lattices::detail::lattice_name_coverage

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

// Each smoke test returns nothing and may be optimized away entirely.
// What survives is the front end checking the call, which is where a
// header that fails to compile under these flags is caught.

// These two bodies are empty on purpose.  The headers they stand for
// assert everything at include time and expose nothing to call, so
// reaching the body is the whole result.
void test_modality_compile() {}

void test_lattice_concepts_compile() {}

void test_graded_runtime_smoke() { ::crucible::algebra::detail::graded_self_test::runtime_smoke_test(); }

void test_qtt_semiring_runtime_smoke() { ::crucible::algebra::lattices::detail::qtt_self_test::runtime_smoke_test(); }

void test_bool_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::bool_lattice_self_test::runtime_smoke_test();
}

void test_conf_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::conf_lattice_self_test::runtime_smoke_test();
}

void test_trust_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::trust_lattice_self_test::runtime_smoke_test();
}

void test_fractional_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::fractional_lattice_self_test::runtime_smoke_test();
}

void test_monotone_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::monotone_lattice_self_test::runtime_smoke_test();
}

void test_seq_prefix_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::seq_prefix_lattice_self_test::runtime_smoke_test();
}

void test_staleness_semiring_runtime_smoke() {
    ::crucible::algebra::lattices::detail::staleness_semiring_self_test::runtime_smoke_test();
}

void test_product_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::product_lattice_self_test::runtime_smoke_test();
}

void test_happens_before_runtime_smoke() {
    ::crucible::algebra::lattices::detail::happens_before_self_test::runtime_smoke_test();
}

void test_chain_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::chain_lattice_self_test::runtime_smoke_test();
}

void test_lifetime_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::lifetime_lattice_self_test::runtime_smoke_test();
}

void test_consistency_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::consistency_lattice_self_test::runtime_smoke_test();
}

void test_tolerance_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::tolerance_lattice_self_test::runtime_smoke_test();
}

void test_det_safe_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::det_safe_lattice_self_test::runtime_smoke_test();
}

void test_hot_path_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::hot_path_lattice_self_test::runtime_smoke_test();
}

void test_wait_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::wait_lattice_self_test::runtime_smoke_test();
}

void test_mem_order_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::mem_order_lattice_self_test::runtime_smoke_test();
}

void test_progress_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::progress_lattice_self_test::runtime_smoke_test();
}

void test_alloc_class_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::alloc_class_lattice_self_test::runtime_smoke_test();
}

void test_bits_budget_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::bits_budget_lattice_self_test::runtime_smoke_test();
}

void test_peak_bytes_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::peak_bytes_lattice_self_test::runtime_smoke_test();
}

void test_epoch_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::epoch_lattice_self_test::runtime_smoke_test();
}

void test_generation_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::generation_lattice_self_test::runtime_smoke_test();
}

void test_numa_node_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::numa_node_lattice_self_test::runtime_smoke_test();
}

void test_affinity_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::affinity_lattice_self_test::runtime_smoke_test();
}

void test_recipe_family_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::recipe_family_lattice_self_test::runtime_smoke_test();
}

void test_cipher_tier_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::cipher_tier_lattice_self_test::runtime_smoke_test();
}

void test_residency_heat_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::residency_heat_lattice_self_test::runtime_smoke_test();
}

void test_vendor_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::vendor_lattice_self_test::runtime_smoke_test();
}

void test_crash_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::crash_lattice_self_test::runtime_smoke_test();
}

void test_witness_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::witness_lattice_self_test::runtime_smoke_test();
}

void test_join_policy_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::join_policy_lattice_self_test::runtime_smoke_test();
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_algebra_compile:\n");

    run_test("test_modality_compile", test_modality_compile);
    run_test("test_lattice_concepts_compile", test_lattice_concepts_compile);
    run_test("test_graded_runtime_smoke", test_graded_runtime_smoke);
    run_test("test_qtt_semiring_runtime_smoke", test_qtt_semiring_runtime_smoke);
    run_test("test_bool_lattice_runtime_smoke", test_bool_lattice_runtime_smoke);
    run_test("test_conf_lattice_runtime_smoke", test_conf_lattice_runtime_smoke);
    run_test("test_trust_lattice_runtime_smoke", test_trust_lattice_runtime_smoke);
    run_test("test_fractional_lattice_runtime_smoke", test_fractional_lattice_runtime_smoke);
    run_test("test_monotone_lattice_runtime_smoke", test_monotone_lattice_runtime_smoke);
    run_test("test_seq_prefix_lattice_runtime_smoke", test_seq_prefix_lattice_runtime_smoke);
    run_test("test_staleness_semiring_runtime_smoke", test_staleness_semiring_runtime_smoke);
    run_test("test_product_lattice_runtime_smoke", test_product_lattice_runtime_smoke);
    run_test("test_happens_before_runtime_smoke", test_happens_before_runtime_smoke);
    run_test("test_chain_lattice_runtime_smoke", test_chain_lattice_runtime_smoke);
    run_test("test_lifetime_lattice_runtime_smoke", test_lifetime_lattice_runtime_smoke);
    run_test("test_consistency_lattice_runtime_smoke", test_consistency_lattice_runtime_smoke);
    run_test("test_tolerance_lattice_runtime_smoke", test_tolerance_lattice_runtime_smoke);
    run_test("test_det_safe_lattice_runtime_smoke", test_det_safe_lattice_runtime_smoke);
    run_test("test_hot_path_lattice_runtime_smoke", test_hot_path_lattice_runtime_smoke);
    run_test("test_wait_lattice_runtime_smoke", test_wait_lattice_runtime_smoke);
    run_test("test_mem_order_lattice_runtime_smoke", test_mem_order_lattice_runtime_smoke);
    run_test("test_progress_lattice_runtime_smoke", test_progress_lattice_runtime_smoke);
    run_test("test_alloc_class_lattice_runtime_smoke", test_alloc_class_lattice_runtime_smoke);
    run_test("test_bits_budget_lattice_runtime_smoke", test_bits_budget_lattice_runtime_smoke);
    run_test("test_peak_bytes_lattice_runtime_smoke", test_peak_bytes_lattice_runtime_smoke);
    run_test("test_epoch_lattice_runtime_smoke", test_epoch_lattice_runtime_smoke);
    run_test("test_generation_lattice_runtime_smoke", test_generation_lattice_runtime_smoke);
    run_test("test_numa_node_lattice_runtime_smoke", test_numa_node_lattice_runtime_smoke);
    run_test("test_affinity_lattice_runtime_smoke", test_affinity_lattice_runtime_smoke);
    run_test("test_recipe_family_lattice_runtime_smoke", test_recipe_family_lattice_runtime_smoke);
    run_test("test_cipher_tier_lattice_runtime_smoke", test_cipher_tier_lattice_runtime_smoke);
    run_test("test_residency_heat_lattice_runtime_smoke", test_residency_heat_lattice_runtime_smoke);
    run_test("test_vendor_lattice_runtime_smoke", test_vendor_lattice_runtime_smoke);
    run_test("test_crash_lattice_runtime_smoke", test_crash_lattice_runtime_smoke);
    run_test("test_witness_lattice_runtime_smoke", test_witness_lattice_runtime_smoke);
    run_test("test_join_policy_lattice_runtime_smoke", test_join_policy_lattice_runtime_smoke);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
