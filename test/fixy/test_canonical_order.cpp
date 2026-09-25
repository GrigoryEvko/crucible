// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, and adds runtime witnesses for stacks
// the header does not already pin.

#include <fixy/CanonicalOrder.h>

#include <cstdint>
#include <cstdio>

namespace co = ::fixy::canonical_order;
namespace fx = ::fixy;
namespace fe = ::foundation::effects;

namespace {

using FromUser = fx::tags::source::FromUser;
using BoundedInt = fx::Refined<fx::bounded_above<int{8}>, int>;

// The header already pins the consteval surface. What follows checks that
// the same predicate gives the same answers at runtime, which is where a
// consteval-versus-runtime divergence would show.

bool test_canonical_layer_index_runtime() {
    if (co::canonical_layer_index_v<fx::HotPath<fx::HotPathTier_v::Hot, int>> != 0) return false;
    if (co::canonical_layer_index_v<fx::DetSafe<fx::DetSafeTier_v::Pure, int>> != 1) return false;
    if (co::canonical_layer_index_v<fx::NumericalTier<fx::Tolerance::BITEXACT, int>> != 2) return false;
    if (co::canonical_layer_index_v<fx::Vendor<fx::VendorBackend_v::NV, int>> != 3) return false;
    if (co::canonical_layer_index_v<fx::ResidencyHeat<fx::ResidencyHeatTag_v::Hot, int>> != 4) return false;
    if (co::canonical_layer_index_v<fx::CipherTier<fx::CipherTierTag_v::Hot, int>> != 5) return false;
    if (co::canonical_layer_index_v<fx::AllocClass<fx::AllocClassTag_v::Arena, int>> != 6) return false;
    if (co::canonical_layer_index_v<fx::Wait<fx::WaitStrategy_v::SpinPause, int>> != 7) return false;
    if (co::canonical_layer_index_v<fx::Stale<int>> != 8) return false;
    if (co::canonical_layer_index_v<fx::Tagged<int, FromUser>> != 9) return false;
    if (co::canonical_layer_index_v<BoundedInt> != 10) return false;
    if (co::canonical_layer_index_v<fx::Secret<int>> != 11) return false;
    if (co::canonical_layer_index_v<fx::Linear<int>> != 12) return false;
    if (co::canonical_layer_index_v<fe::Computation<fe::Row<>, int>> != 13) return false;
    return true;
}

bool test_canonically_ordered_concept() {
    static_assert(co::CanonicallyOrdered<int>);
    static_assert(co::CanonicallyOrdered<fx::Linear<int>>);
    static_assert(co::CanonicallyOrdered<fx::HotPath<fx::HotPathTier_v::Hot, fx::Linear<int>>>);

    static_assert(!co::CanonicallyOrdered<fx::Linear<fx::HotPath<fx::HotPathTier_v::Hot, int>>>);
    return true;
}

bool test_inverted_stacks_runtime() {
    using BadA = fx::Linear<fx::HotPath<fx::HotPathTier_v::Hot, int>>;
    using BadB = fx::Refined<fx::bounded_above<int{8}>, fx::Tagged<fx::Stale<int>, FromUser>>;
    using BadC = fx::HotPath<fx::HotPathTier_v::Hot, fx::HotPath<fx::HotPathTier_v::Cold, int>>;
    if (co::is_canonically_ordered_v<BadA>) return false;
    if (co::is_canonically_ordered_v<BadB>) return false;
    if (co::is_canonically_ordered_v<BadC>) return false;
    return true;
}

bool test_canonical_full_stack_runtime() {
    using Good = fx::HotPath<
        fx::HotPathTier_v::Hot,
        fx::DetSafe<fx::DetSafeTier_v::Pure,
                    fx::NumericalTier<fx::Tolerance::BITEXACT,
                                      fx::Vendor<fx::VendorBackend_v::NV, fe::Computation<fe::Row<>, int>>>>>;
    return co::is_canonically_ordered_v<Good>;
}

bool test_off_tree_neutrality() {
    // Stale, Tagged and Refined sit at layers 8, 9 and 10, so that nesting
    // is canonical. A wrapper that is off the tree must not disrupt the
    // descent when it sits in the middle of one. The lifetime band has no
    // canonical position, so it is off the tree.
    using OffTree = fx::Stale<fx::Tagged<fx::opaque_lifetime::PerRequest<BoundedInt>, FromUser>>;
    return co::is_canonically_ordered_v<OffTree>;
}

}  // namespace

int main() {
    int failures = 0;
    auto run = [&](const char* name, bool (*fn)()) {
        std::printf("  %-40s ", name);
        const bool is_passed = fn();
        std::printf("%s\n", is_passed ? "PASSED" : "FAILED");
        if (!is_passed) ++failures;
    };

    std::puts("test_canonical_order:");
    run("layer_index_runtime", test_canonical_layer_index_runtime);
    run("canonically_ordered_concept", test_canonically_ordered_concept);
    run("inverted_stacks_rejected", test_inverted_stacks_runtime);
    run("canonical_full_stack", test_canonical_full_stack_runtime);
    run("off_tree_neutrality", test_off_tree_neutrality);

    std::printf("test_canonical_order: %s (%d failure%s)\n", failures == 0 ? "all passed" : "FAILED", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
