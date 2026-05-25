// FIXY-FOUND-048 — sentinel TU for safety::diag::canonical_order
//
// Forces include of safety/diag/CanonicalOrder.h under project
// warning flags so the header's embedded static_asserts execute.
// Adds additional runtime asserts to witness the predicate's
// integer return + concept satisfaction across canonical and
// inverted stacks the header doesn't already pin.

#include <crucible/safety/diag/CanonicalOrder.h>
#include <crucible/safety/Witness.h>  // off-tree neutrality probe
#include <crucible/algebra/lattices/MemOrderLattice.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>

namespace co = crucible::safety::diag::canonical_order;
namespace cs = crucible::safety;
namespace ce = crucible::effects;
namespace al = crucible::algebra::lattices;

namespace {

// ── Runtime smoke witnesses (header's static_asserts already pin
//    the consteval surface).  Below verifies the same predicate
//    evaluates to the same answers at runtime — guarding against a
//    consteval-vs-runtime regression.

bool test_canonical_layer_index_runtime() {
    if (co::canonical_layer_index_v<cs::HotPath<cs::HotPathTier_v::Hot, int>>           !=  0) return false;
    if (co::canonical_layer_index_v<cs::DetSafe<cs::DetSafeTier_v::Pure, int>>          !=  1) return false;
    if (co::canonical_layer_index_v<cs::NumericalTier<al::Tolerance::BITEXACT, int>>    !=  2) return false;
    if (co::canonical_layer_index_v<cs::Vendor<cs::VendorBackend_v::NV, int>>           !=  3) return false;
    if (co::canonical_layer_index_v<cs::ResidencyHeat<cs::ResidencyHeatTag_v::Hot, int>> != 4) return false;
    if (co::canonical_layer_index_v<cs::CipherTier<cs::CipherTierTag_v::Hot, int>>      !=  5) return false;
    if (co::canonical_layer_index_v<cs::AllocClass<cs::AllocClassTag_v::Arena, int>>    !=  6) return false;
    if (co::canonical_layer_index_v<cs::Wait<cs::WaitStrategy_v::SpinPause, int>>       !=  7) return false;
    if (co::canonical_layer_index_v<cs::MemOrder<cs::MemOrderTag_v::SeqCst, int>>       !=  8) return false;
    if (co::canonical_layer_index_v<cs::Progress<cs::ProgressClass_v::Bounded, int>>    !=  9) return false;
    if (co::canonical_layer_index_v<cs::Stale<int>>                                     != 10) return false;
    if (co::canonical_layer_index_v<cs::Tagged<int, cs::source::FromUser>>              != 11) return false;
    if (co::canonical_layer_index_v<cs::Refined<cs::bounded_above<int{8}>, int>>        != 12) return false;
    if (co::canonical_layer_index_v<cs::Secret<int>>                                    != 13) return false;
    if (co::canonical_layer_index_v<cs::Linear<int>>                                    != 14) return false;
    if (co::canonical_layer_index_v<ce::Computation<ce::Row<>, int>>                    != 15) return false;
    return true;
}

bool test_canonically_ordered_concept() {
    // Concept satisfies expected types.
    static_assert(co::CanonicallyOrdered<int>);
    static_assert(co::CanonicallyOrdered<cs::Linear<int>>);
    static_assert(co::CanonicallyOrdered<
        cs::HotPath<cs::HotPathTier_v::Hot, cs::Linear<int>>>);

    // Concept rejects inverted stack at instantiation site.
    static_assert(!co::CanonicallyOrdered<
        cs::Linear<cs::HotPath<cs::HotPathTier_v::Hot, int>>>);
    return true;
}

bool test_inverted_stacks_runtime() {
    using BadA = cs::Linear<cs::HotPath<cs::HotPathTier_v::Hot, int>>;
    using BadB = cs::Refined<cs::bounded_above<int{8}>,
                    cs::Tagged<cs::Stale<int>, cs::source::FromUser>>;
    using BadC = cs::HotPath<cs::HotPathTier_v::Hot,
                    cs::HotPath<cs::HotPathTier_v::Cold, int>>;
    if (co::is_canonically_ordered_v<BadA>) return false;
    if (co::is_canonically_ordered_v<BadB>) return false;
    if (co::is_canonically_ordered_v<BadC>) return false;
    return true;
}

bool test_canonical_full_stack_runtime() {
    // §XVI canonical 4-layer example from CLAUDE.md.
    using Good = cs::HotPath<cs::HotPathTier_v::Hot,
                    cs::DetSafe<cs::DetSafeTier_v::Pure,
                        cs::NumericalTier<al::Tolerance::BITEXACT,
                            cs::Vendor<cs::VendorBackend_v::NV,
                                ce::Computation<ce::Row<>, int>>>>>;
    return co::is_canonically_ordered_v<Good>;
}

bool test_off_tree_neutrality() {
    // Tagged (11) wrapping Refined (12) inside Stale (10) is canonical;
    // but if we PUT an off-tree wrapper like Witness in the middle, it
    // must NOT disrupt the descent.  Use cs::Witness as the off-tree
    // probe.
    using OffTree = cs::Stale<cs::Tagged<cs::Witness<cs::Witness_v::FORMALLY_VERIFIED,
                                  cs::Refined<cs::bounded_above<int{8}>, int>>,
                                  cs::source::FromUser>>;
    // Stale(10) ⊃ Tagged(11) ⊃ Witness(off-tree) ⊃ Refined(12) → ordered.
    return co::is_canonically_ordered_v<OffTree>;
}

}  // namespace

int main() {
    int failures = 0;
    auto run = [&](const char* name, bool (*fn)()) {
        std::printf("  %-40s ", name);
        const bool ok = fn();
        std::printf("%s\n", ok ? "PASSED" : "FAILED");
        if (!ok) ++failures;
    };

    std::puts("test_canonical_order (FIXY-FOUND-048):");
    run("layer_index_runtime",         test_canonical_layer_index_runtime);
    run("canonically_ordered_concept", test_canonically_ordered_concept);
    run("inverted_stacks_rejected",    test_inverted_stacks_runtime);
    run("canonical_full_stack",        test_canonical_full_stack_runtime);
    run("off_tree_neutrality",         test_off_tree_neutrality);

    std::printf("test_canonical_order: %s (%d failure%s)\n",
        failures == 0 ? "all passed" : "FAILED",
        failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
