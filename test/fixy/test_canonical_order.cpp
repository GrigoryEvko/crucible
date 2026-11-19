// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags, and adds runtime witnesses for stacks
// the header does not already pin.

#include <fixy/CanonicalOrder.h>
#include <fixy/Core.h>

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <utility>

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
    if (co::canonical_layer_index_v<fx::DetSafe<fx::DetSafeTier_v::Pure, int>> != 0) return false;
    if (co::canonical_layer_index_v<fx::NumericalTier<fx::Tolerance::BITEXACT, int>> != 1) return false;
    if (co::canonical_layer_index_v<fx::Vendor<fx::VendorBackend_v::NV, int>> != 2) return false;
    if (co::canonical_layer_index_v<fx::ResidencyHeat<fx::ResidencyHeatTag_v::Hot, int>> != 3) return false;
    if (co::canonical_layer_index_v<fx::CipherTier<fx::CipherTierTag_v::Hot, int>> != 4) return false;
    if (co::canonical_layer_index_v<fx::AllocClass<fx::AllocClassTag_v::Arena, int>> != 5) return false;
    if (co::canonical_layer_index_v<fx::Wait<fx::WaitStrategy_v::SpinPause, int>> != 6) return false;
    if (co::canonical_layer_index_v<fx::Stale<int>> != 7) return false;
    if (co::canonical_layer_index_v<fx::Tagged<int, FromUser>> != 8) return false;
    if (co::canonical_layer_index_v<BoundedInt> != 9) return false;
    if (co::canonical_layer_index_v<fx::Secret<int>> != 10) return false;
    if (co::canonical_layer_index_v<fx::Linear<int>> != 11) return false;
    if (co::canonical_layer_index_v<fe::Computation<fe::Row<>, int>> != 12) return false;
    return true;
}

bool test_canonically_ordered_concept() {
    static_assert(co::CanonicallyOrdered<int>);
    static_assert(co::CanonicallyOrdered<fx::Linear<int>>);
    static_assert(co::CanonicallyOrdered<fx::DetSafe<fx::DetSafeTier_v::Pure, fx::Linear<int>>>);

    static_assert(!co::CanonicallyOrdered<fx::Linear<fx::DetSafe<fx::DetSafeTier_v::Pure, int>>>);
    return true;
}

bool test_inverted_stacks_runtime() {
    using BadA = fx::Linear<fx::DetSafe<fx::DetSafeTier_v::Pure, int>>;
    using BadB = fx::Refined<fx::bounded_above<int{8}>, fx::Tagged<fx::Stale<int>, FromUser>>;
    using BadC = fx::DetSafe<fx::DetSafeTier_v::Pure, fx::DetSafe<fx::DetSafeTier_v::PhiloxRng, int>>;
    if (co::is_canonically_ordered_v<BadA>) return false;
    if (co::is_canonically_ordered_v<BadB>) return false;
    if (co::is_canonically_ordered_v<BadC>) return false;
    return true;
}

bool test_canonical_full_stack_runtime() {
    using Good = fx::DetSafe<
        fx::DetSafeTier_v::Pure,
        fx::NumericalTier<fx::Tolerance::BITEXACT,
                          fx::Vendor<fx::VendorBackend_v::NV,
                                     fx::CipherTier<fx::CipherTierTag_v::Hot, fe::Computation<fe::Row<>, int>>>>>;
    return co::is_canonically_ordered_v<Good>;
}

bool test_off_tree_neutrality() {
    // Stale, Tagged and Refined sit at layers 7, 8 and 9, so that nesting
    // is canonical. A wrapper that is off the tree must not disrupt the
    // descent when it sits in the middle of one. The lifetime band has no
    // canonical position, so it is off the tree.
    using OffTree = fx::Stale<fx::Tagged<fx::opaque_lifetime::PerRequest<BoundedInt>, FromUser>>;
    return co::is_canonically_ordered_v<OffTree>;
}

// A container of four elements, in the shape that the walk reads in a std
// container: it names the type of its elements, and it gives a cursor to
// its first element and past its last.
template <class Element>
struct FourCells final {
    using value_type = Element;
    [[nodiscard]] constexpr int begin() const noexcept { return 0; }
    [[nodiscard]] constexpr int end() const noexcept { return 4; }
};

// A container stops the run of the walk: its element is a value of its
// own.  A refined list of tagged values is in order, although Tagged sits
// above Refined in the order.
bool test_container_element_starts_a_run() {
    using RefinedList = fx::Refined<fx::bounded_above<int{8}>, FourCells<fx::Tagged<int, FromUser>>>;
    static_assert(co::CanonicallyOrdered<RefinedList>);
    using ListOfInverted = FourCells<fx::Refined<fx::bounded_above<int{8}>, fx::Tagged<int, FromUser>>>;
    static_assert(!co::CanonicallyOrdered<ListOfInverted>, "an element still keeps its own order");
    return co::is_canonically_ordered_v<RefinedList> && !co::is_canonically_ordered_v<ListOfInverted>;
}

// ── Each two-wrapper stack of Refined, Secret and Linear ─────────────
//
// The order puts Refined outside Secret, and Secret outside Linear.  The two
// stacks under Refined mint through the checked mints, with no trusted mint,
// and pass the walk.  A predicate takes its value by reference to const, so it reads
// a value that cannot be copied through the read door of its wrapper: peek
// for a Linear, and the LengthOnly declassification for a Secret.

// A predicate on a Linear<int>, through peek.
struct LinearHoldsPositive final {
    constexpr bool operator()(std::same_as<fx::Linear<int>> auto const& handle) const noexcept {
        return handle.peek() > 0;
    }
};

inline constexpr LinearHoldsPositive linear_holds_positive{};

// A key of eight bytes, the payload of the Secret below.  It answers its
// length, which is the one fact that the LengthOnly policy releases.
struct EightByteKey final {
    unsigned long long bits = 0;
    [[nodiscard]] constexpr unsigned long size() const noexcept { return sizeof(bits); }
};

// A predicate on a Secret<EightByteKey>, through the LengthOnly door.
struct SecretKeyHasEightBytes final {
    constexpr bool operator()(std::same_as<fx::Secret<EightByteKey>> auto const& key) const noexcept {
        return key.template declassify<fx::tags::secret_policy::LengthOnly>() == 8;
    }
};

inline constexpr SecretKeyHasEightBytes secret_key_has_eight_bytes{};

bool test_each_pair_of_the_three_layers_mints_checked() {
    fx::Refined<linear_holds_positive, fx::Linear<int>> refined_linear =
        fx::mint_refined<linear_holds_positive>(fx::mint_linear<int>(7));
    static_assert(co::CanonicallyOrdered<decltype(refined_linear)>);

    fx::Refined<secret_key_has_eight_bytes, fx::Secret<EightByteKey>> refined_secret =
        fx::mint_refined<secret_key_has_eight_bytes>(fx::mint_secret<EightByteKey>());
    static_assert(co::CanonicallyOrdered<decltype(refined_secret)>);

    // Secret<Linear<T>> is in order too.  The door of a call builds the
    // Linear in its place in the Secret, so no closure holds it by reference.
    fx::Secret<fx::Linear<int>> secret_linear =
        fx::mint_secret_from_call([]() noexcept { return fx::mint_linear<int>(9); });
    static_assert(co::CanonicallyOrdered<decltype(secret_linear)>);

    static_assert(!co::CanonicallyOrdered<fx::Linear<fx::Refined<fx::positive, int>>>);
    static_assert(!co::CanonicallyOrdered<fx::Secret<fx::Refined<fx::positive, int>>>);
    static_assert(!co::CanonicallyOrdered<fx::Linear<fx::Secret<int>>>);

    const bool is_linear_held = refined_linear.value().peek() == 7;
    const bool is_key_held = secret_key_has_eight_bytes(refined_secret.value());
    const int consumed = std::move(refined_linear).into().consume();
    const int released =
        std::move(secret_linear).template declassify<fx::tags::secret_policy::AuthorizedReplay>().consume();
    return is_linear_held && is_key_held && consumed == 7 && released == 9;
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
    run("container_element_starts_a_run", test_container_element_starts_a_run);
    run("each_pair_of_three_layers_mints_checked", test_each_pair_of_the_three_layers_mints_checked);

    std::printf("test_canonical_order: %s (%d failure%s)\n", failures == 0 ? "all passed" : "FAILED", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
