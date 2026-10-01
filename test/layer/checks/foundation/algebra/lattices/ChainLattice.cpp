// The compile-time checks of foundation/algebra/lattices/ChainLattice.h.

#include <foundation/algebra/lattices/ChainLattice.h>

namespace foundation::algebra::lattices {

// The smoke chain of the header holds an enum that is independent of
// every production caller, so the base is verified for an arbitrary
// scoped-enum ordinal layout.
namespace detail::chain_lattice_self_test {

static_assert(verify_chain_lattice_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy bounded-lattice axioms for an "
              "arbitrary scoped enum");
static_assert(verify_chain_lattice_distributive_exhaustive<SmokeChainLattice>(),
              "ChainLatticeOps must satisfy distributive-lattice axioms for "
              "an arbitrary scoped enum");
static_assert(verify_enum_lattice_exhaustive<SmokeChainLattice>());
static_assert(verify_chain_lattice<SmokeChainLattice>(),
              "The generic chain self-test must accept the smoke chain, which "
              "is declared in order with one At per tier.");

// The base reads the bounds and the name by reflection, and it takes the
// orientation as the argument states it.
static_assert(SmokeChainLattice::bottom() == SmokeTier::Lo);
static_assert(SmokeChainLattice::top() == SmokeTier::Hi);
static_assert(SmokeChainLattice::name() == "SmokeChainLattice");
static_assert(claim_orientation_v<SmokeChainLattice> == ClaimOrientation::stronger_is_higher);
static_assert(!GradableLattice<SmokeChainLattice>);
static_assert(claim_orientation_v<SmokeChainLattice::At<SmokeTier::Mid>> == ClaimOrientation::one_claim);

// A chain base with an unstated orientation, or with the one-claim
// orientation of a single element, does not compile.
template <ClaimOrientation Orientation>
concept chain_base_accepts = requires { typename EnumChainLattice<SmokeChainLattice, SmokeTier, Orientation>; };
static_assert(chain_base_accepts<ClaimOrientation::weaker_is_higher>);
static_assert(!chain_base_accepts<ClaimOrientation::unstated> && !chain_base_accepts<ClaimOrientation::one_claim>);

// The generic walk pins the shape; these cells pin the exact spellings
// that the reflection builds.
static_assert(SmokeChainLattice::At<SmokeTier::Mid>::name() == "SmokeChainLattice::At<Mid>");
static_assert(SmokeChainLattice::At<static_cast<SmokeTier>(9)>::name() == "SmokeChainLattice::At<?>");
static_assert(pinned_at_sentinel_v<SmokeChainLattice> == "SmokeChainLattice::At<?>");
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::tier == SmokeTier::Hi);
static_assert(SmokeChainLattice::At<SmokeTier::Hi>::pinned == SmokeTier::Hi);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::enum_type, SmokeTier>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::outer_lattice, SmokeChainLattice>);
static_assert(std::is_same_v<SmokeChainLattice::At<SmokeTier::Hi>::element_type::pinned_value_type, SmokeTier>);
static_assert(std::is_empty_v<SmokeChainLattice::At<SmokeTier::Lo>::element_type>);
static_assert(BoundedLattice<SmokeChainLattice::At<SmokeTier::Lo>>);

// Each pinned grade of each lattice has the element PinnedElement<v>, and
// verify_pinned_at checks that.  These cells check the layout of a
// carrier over that element: a class value and an arithmetic value each
// keep their size, alignment and trivial properties, under the modality
// that fixes a grade and under the one that takes the value back out.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using PinnedAbsolute = Graded<ModalityKind::Absolute, SmokeChainLattice::At<SmokeTier::Mid>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedAbsolute, double);

template <typename T_>
using PinnedComonad = Graded<ModalityKind::Comonad, SmokeChainLattice::At<SmokeTier::Hi>, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PinnedComonad, double);

// A chain declared out of order fails the declaration-order walk, and a
// chain whose bottom is not its first enumerator fails the bounds check.
// The negative direction of the generic self-test is witnessed here, and
// a refactor cannot turn it into a tautology.
enum class ReversedTier : std::uint8_t {
    Hi = 2,
    Mid = 1,
    Lo = 0
};

struct ReversedChainLattice : ChainLatticeOps<ReversedTier> {
    [[nodiscard]] static constexpr ReversedTier bottom() noexcept { return ReversedTier::Lo; }
    [[nodiscard]] static constexpr ReversedTier top() noexcept { return ReversedTier::Hi; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "ReversedChainLattice"; }

    template <ReversedTier T>
    struct At : PinnedAt<ReversedChainLattice, T> {
        static constexpr ReversedTier tier = T;
    };
};

static_assert(verify_chain_lattice_exhaustive<ReversedChainLattice>(),
              "The lattice laws do not care about declaration order.");
static_assert(!verify_chain_lattice<ReversedChainLattice>(),
              "The generic chain self-test must reject a chain whose declaration "
              "order is not its lattice order.");
static_assert(verify_pinned_at<ReversedChainLattice>(), "The pinned-grade walk does not depend on declaration order.");

// EnumChainLattice over the reversed enum reads the first enumerator as
// bottom.  That bottom sits above its top, and the result is not a
// lattice.
struct ReversedEnumChain : EnumChainLattice<ReversedEnumChain, ReversedTier, ClaimOrientation::stronger_is_higher> {};
static_assert(!Lattice<ReversedEnumChain>,
              "A chain base over an enum whose values fall in declaration order must not be a lattice.");

}  // namespace detail::chain_lattice_self_test

}  // namespace foundation::algebra::lattices
