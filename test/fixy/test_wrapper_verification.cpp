// Each fixy wrapper ships its own test, and each of those covers one
// wrapper alone.  Nothing else checks that the wrappers agree with each
// other.  This translation unit is the one place where they all meet,
// so it asserts the properties that only a cross-wrapper view can see:
//
//   - every wrapper exposes the same diagnostic surface, so a
//     forwarder that one wrapper loses and the others keep fails here
//   - every wrapper preserves sizeof, alignof and the two trivialities
//     wherever its storage regime promises it, and states its regime
//     where it does not
//   - every wrapper instantiates as the inner type of every other
//   - the two nesting orders of a pair are distinct types
//
// Wrapper-local behavior, such as construction, peek, consume and
// contracts, belongs to the per-wrapper tests.  This file does not
// repeat it beyond one smoke call per wrapper, which is there so that a
// header-only regression cannot hide behind static_asserts alone.
//
// Old spelling: test/test_migration_verification.cpp, over the old
// safety wrappers.  Three things it carried are not here.  The row-hash
// cells wait on the row-hash port, which is what gives the new tree a
// row hash at all; until then the nesting cells pin type distinctness
// only.  The
// wrappers the new tree did not carry (Consistency, Crash, Progress,
// MemOrder, ResidencyHeat, Vendor, Budgeted, EpochVersioned,
// NumaPlacement, TimeOrdered) have no cell, because they have no type.
// SharedPermission is a foundation type and is verified in
// test/foundation/test_permission_shared.cpp.

#include <fixy/Bands.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>

#include <compare>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fa = ::foundation::algebra;
using namespace ::fixy;
using ::fixy::tags::source::FromUser;

// A local predicate keeps these assertions independent of the
// predicates that the shared headers ship.
inline constexpr auto positive_local = [](int x) constexpr noexcept { return x > 0; };

// A predicate that admits every value of every type, for the layout
// cells: they measure storage, not admission.
inline constexpr auto any_value = [](auto const&) constexpr noexcept { return true; };

// Sixteen bytes, two words, ordered so Monotonic can carry it.
struct TwoWords {
    std::uint64_t first;
    std::uint64_t second;
    constexpr auto operator<=>(TwoWords const&) const noexcept = default;
};
static_assert(sizeof(TwoWords) == 16);

// ── Layout, per storage regime ───────────────────────────────────────
//
// A wrapper whose grade is a single type-level point stores nothing of
// its own and collapses to the storage of T, and keeps T's alignment
// and its two trivialities: that is what CRUCIBLE_GRADED_LAYOUT_INVARIANT
// asserts, for int, double, a pointer and a sixteen-byte struct.  Two
// wrappers carry a grade beside the value and state their own bound.

template <class T>
using RefinedAny = Refined<any_value, T>;
template <class T>
using SealedAny = SealedRefined<any_value, T>;
template <class T>
using TaggedUser = Tagged<T, FromUser>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(Linear, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Linear, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Linear, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Linear, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(Affine, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Affine, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Affine, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Affine, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedAny, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedAny, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedAny, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RefinedAny, TwoWords);

// Sealing removes the rvalue extractor from the API and changes no
// storage.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedAny, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedAny, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedAny, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedAny, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedUser, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedUser, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedUser, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TaggedUser, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(Secret, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Secret, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Secret, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Secret, TwoWords);

// Monotonic's grade is its value, so the substrate holds one cell
// instead of a value and a grade.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Monotonic, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Monotonic, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Monotonic, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Monotonic, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(det_safe::Pure, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(det_safe::Pure, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(det_safe::Pure, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(det_safe::Pure, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(alloc_class::Arena, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(alloc_class::Arena, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(alloc_class::Arena, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(alloc_class::Arena, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(hot_path::Hot, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(hot_path::Hot, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(hot_path::Hot, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(hot_path::Hot, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(cipher_tier::Warm, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(cipher_tier::Warm, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(cipher_tier::Warm, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(cipher_tier::Warm, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(wait::SpinPause, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(wait::SpinPause, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(wait::SpinPause, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(wait::SpinPause, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(numerical_tier::Bitexact, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(numerical_tier::Bitexact, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(numerical_tier::Bitexact, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(numerical_tier::Bitexact, TwoWords);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(opaque_lifetime::PerFleet, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(opaque_lifetime::PerFleet, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(opaque_lifetime::PerFleet, int*);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(opaque_lifetime::PerFleet, TwoWords);

// Stale carries its staleness beside the value; the bound is the value
// plus that field, and the wrapper keeps the value's trivialities.
static_assert(sizeof(Stale<int>) >= sizeof(int) + sizeof(Stale<int>::staleness_t));
static_assert(sizeof(Stale<TwoWords>) >= sizeof(TwoWords) + sizeof(Stale<TwoWords>::staleness_t));
static_assert(std::is_trivially_copyable_v<Stale<int>>);
static_assert(std::is_trivially_destructible_v<Stale<int>>);

// AppendOnly grades a container and derives the grade from its size,
// so it costs exactly the container.
static_assert(sizeof(AppendOnly<int>) == sizeof(std::vector<int>));
static_assert(sizeof(AppendOnly<TwoWords>) == sizeof(std::vector<TwoWords>));

// RecipeSpec stores both numerical axes beside the value.
static_assert(sizeof(RecipeSpec<int>) >= sizeof(int) + 2);

// ── Nesting order is a type, not a spelling ─────────────────────────
//
// The row hash that turns nesting order into a federation cache slot
// arrives with the row-hash port.  Until then, what can be pinned is
// that both orders instantiate and are distinct types.

static_assert(!std::is_same_v<Stale<Tagged<int, FromUser>>, Tagged<Stale<int>, FromUser>>);
static_assert(!std::is_same_v<Refined<positive_local, Linear<int>>, Linear<Refined<positive_local, int>>>);
static_assert(!std::is_same_v<Secret<Tagged<int, FromUser>>, Tagged<Secret<int>, FromUser>>);
static_assert(!std::is_same_v<hot_path::Hot<numerical_tier::Bitexact<int>>, numerical_tier::Bitexact<hot_path::Hot<int>>>);
static_assert(!std::is_same_v<cipher_tier::Hot<det_safe::Pure<int>>, det_safe::Pure<cipher_tier::Hot<int>>>);

// ── The diagnostic surface ──────────────────────────────────────────
//
// The value name comes from reflection.  The spelling of a primitive
// type depends on the surrounding context, so these match a suffix and
// not the whole string.

static_assert(Linear<int>::value_type_name().ends_with("int"));
static_assert(Affine<int>::value_type_name().ends_with("int"));
static_assert(Refined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(SealedRefined<positive_local, int>::value_type_name().ends_with("int"));
static_assert(Tagged<int, FromUser>::value_type_name().ends_with("int"));
static_assert(Secret<int>::value_type_name().ends_with("int"));
static_assert(Monotonic<std::uint64_t>::value_type_name().ends_with("uint64_t")
              || Monotonic<std::uint64_t>::value_type_name().ends_with("long unsigned int"));
static_assert(Stale<int>::value_type_name().ends_with("int"));
static_assert(det_safe::Pure<int>::value_type_name().ends_with("int"));
static_assert(numerical_tier::Bitexact<double>::value_type_name().ends_with("double"));
static_assert(RecipeSpec<double>::value_type_name().ends_with("double"));

// graded_type is public on every wrapper.  A refactor that moves it
// back into a private section makes the name ill-formed here.
static_assert(!std::is_void_v<typename Linear<int>::graded_type>);
static_assert(!std::is_void_v<typename Affine<int>::graded_type>);
static_assert(!std::is_void_v<typename Refined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename SealedRefined<positive_local, int>::graded_type>);
static_assert(!std::is_void_v<typename Tagged<int, FromUser>::graded_type>);
static_assert(!std::is_void_v<typename Secret<int>::graded_type>);
static_assert(!std::is_void_v<typename Monotonic<std::uint64_t>::graded_type>);
static_assert(!std::is_void_v<typename AppendOnly<int>::graded_type>);
static_assert(!std::is_void_v<typename Stale<int>::graded_type>);

// The concept folds the separate forwarder checks into one contract.
// A wrapper that breaks a forwarder fails here, and the diagnostic
// names it.

static_assert(fa::GradedWrapper<Linear<int>>);
static_assert(fa::GradedWrapper<Affine<int>>);
static_assert(fa::GradedWrapper<Refined<positive_local, int>>);
static_assert(fa::GradedWrapper<SealedRefined<positive_local, int>>);
static_assert(fa::GradedWrapper<Tagged<int, FromUser>>);
static_assert(fa::GradedWrapper<Secret<int>>);
static_assert(fa::GradedWrapper<Monotonic<std::uint64_t>>);
static_assert(fa::GradedWrapper<AppendOnly<int>>);
static_assert(fa::GradedWrapper<Stale<int>>);

// A band is the substrate itself, not a wrapper around it.
// GradedWrapper asks for graded_type, the wrapper's link to its
// substrate, and Graded has none, so the question a band answers is
// IsGraded and IsBand.  The two questions stay distinct: the first
// negative cell below is what pins that a substrate is not admitted as
// a wrapper.
static_assert(!fa::GradedWrapper<det_safe::Pure<int>>);
static_assert(fa::IsGraded<det_safe::Pure<int>> && IsBand<det_safe::Pure<int>>);
static_assert(fa::IsGraded<alloc_class::Arena<int>> && IsBand<alloc_class::Arena<int>>);
static_assert(fa::IsGraded<hot_path::Hot<int>> && IsBand<hot_path::Hot<int>>);
static_assert(fa::IsGraded<cipher_tier::Warm<int>> && IsBand<cipher_tier::Warm<int>>);
static_assert(fa::IsGraded<wait::SpinPause<int>> && IsBand<wait::SpinPause<int>>);
static_assert(fa::IsGraded<numerical_tier::Bitexact<int>> && IsBand<numerical_tier::Bitexact<int>>);
static_assert(fa::IsGraded<opaque_lifetime::PerFleet<int>> && IsBand<opaque_lifetime::PerFleet<int>>);
static_assert(fa::IsGraded<RecipeSpec<int>> && IsRecipeSpec<RecipeSpec<int>> && !IsBand<RecipeSpec<int>>);
static_assert(IsBandOf<DetSafeLattice, det_safe::Pure<int>> && !IsBandOf<HotPathLattice, det_safe::Pure<int>>);

// The variable form tracks the concept, so two spot checks are enough.
static_assert(fa::is_graded_wrapper_v<Linear<int>>);
static_assert(fa::is_graded_wrapper_v<Stale<int>>);
static_assert(!fa::is_graded_wrapper_v<int>);
static_assert(!fa::is_graded_wrapper_v<void*>);
static_assert(!fa::is_graded_wrapper_v<std::string_view>);

// The concept compares each forwarder's string with the substrate's.
// This repeats that comparison outside the concept, so that the two
// cannot drift together.
template <typename W>
[[nodiscard]] consteval bool forwarders_actually_forward() noexcept {
    return W::value_type_name() == W::graded_type::value_type_name()
        && W::lattice_name() == W::graded_type::lattice_name();
}

static_assert(forwarders_actually_forward<Linear<int>>());
static_assert(forwarders_actually_forward<Affine<int>>());
static_assert(forwarders_actually_forward<Refined<positive_local, int>>());
static_assert(forwarders_actually_forward<SealedRefined<positive_local, int>>());
static_assert(forwarders_actually_forward<Tagged<int, FromUser>>());
static_assert(forwarders_actually_forward<Secret<int>>());
static_assert(forwarders_actually_forward<Monotonic<std::uint64_t>>());
static_assert(forwarders_actually_forward<AppendOnly<int>>());
static_assert(forwarders_actually_forward<Stale<int>>());

// The lattice names the tree pins elsewhere are pinned here again, so
// a rename shows up in the one file that sees every wrapper.  The
// refinement's name comes from its predicate and is compared between
// the two refinements rather than against a literal.  A band's name
// carries the tier and not the payload.
static_assert(Linear<int>::lattice_name() == "QttSemiring::At<1>");
static_assert(Affine<int>::lattice_name() == "QttSemiring::At<0>");
static_assert(Refined<positive_local, int>::lattice_name() == SealedRefined<positive_local, int>::lattice_name());
static_assert(Secret<int>::lattice_name() == "ConfLattice::At<Secret>");
static_assert(Monotonic<std::uint64_t>::lattice_name() == "MonotoneLattice");
static_assert(AppendOnly<int>::lattice_name() == "SeqPrefixLattice");
static_assert(Stale<int>::lattice_name() == "StalenessSemiring");
static_assert(det_safe::Pure<int>::lattice_name() == det_safe::Pure<double>::lattice_name());
static_assert(det_safe::Pure<int>::lattice_name() != det_safe::PhiloxRng<int>::lattice_name());
static_assert(hot_path::Hot<int>::lattice_name() != cipher_tier::Hot<int>::lattice_name(),
              "Two tier axes that spell the same tier must keep distinct lattice names, or a diagnostic "
              "cannot tell an execution budget from a storage tier.");

// ── Composition ─────────────────────────────────────────────────────
//
// Every wrapper instantiates as the inner type of every other, and a
// collapsing wrapper outside a value keeps the value's storage.

using TaggedLinear = Tagged<Linear<int>, FromUser>;
static_assert(sizeof(TaggedLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<TaggedLinear>,
              "Tagged<Linear<T>> must preserve Linear's move-only discipline.");

// There is no Refined<P, Linear<T>> layout cell with a real predicate.
// The predicate runs on the wrapped value, so such a composition needs
// a predicate invocable on Linear<T>; RefinedLinear in Refined.h is
// that spelling and its own test covers it.

using SecretRefined = Secret<Refined<positive_local, int>>;
static_assert(sizeof(SecretRefined) == sizeof(int));
static_assert(fa::GradedWrapper<SecretRefined>);

using LinearSealed = Linear<SealedRefined<positive_local, int>>;
static_assert(sizeof(LinearSealed) == sizeof(int));

using TaggedStale = Tagged<Stale<int>, FromUser>;
// Stale's counter, and not Tagged, sets the size here.
static_assert(sizeof(TaggedStale) == sizeof(Stale<int>));

using TaggedBitexact = Tagged<numerical_tier::Bitexact<int>, FromUser>;
static_assert(sizeof(TaggedBitexact) == sizeof(int),
              "Tagged<NumericalTier<T>, Source> must collapse to sizeof(T): two regime-1 wrappers "
              "stacked, both with an empty grade.");

using BitexactOverLinear = numerical_tier::Bitexact<Linear<int>>;
static_assert(sizeof(BitexactOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<BitexactOverLinear>,
              "NumericalTier<Linear<T>> must preserve Linear's move-only discipline.  If this fires, "
              "Linear's copy-deletion is no longer visible through the band.");
static_assert(std::is_move_constructible_v<BitexactOverLinear>);

using Fp32OverRefined = numerical_tier::Fp32<Refined<positive_local, int>>;
static_assert(sizeof(Fp32OverRefined) == sizeof(int));
static_assert(IsBand<Fp32OverRefined>);

using PureOverLinear = det_safe::Pure<Linear<int>>;
static_assert(sizeof(PureOverLinear) == sizeof(int));
static_assert(!std::is_copy_constructible_v<PureOverLinear>);
static_assert(std::is_move_constructible_v<PureOverLinear>);

using HotOverStale = hot_path::Hot<Stale<int>>;
static_assert(sizeof(HotOverStale) == sizeof(Stale<int>),
              "HotPath<Stale<T>> must collapse the HotPath grade only: Stale carries a runtime grade "
              "beside T.");
static_assert(IsBand<HotOverStale>);

using StaleOverHot = Stale<hot_path::Hot<int>>;
static_assert(sizeof(StaleOverHot) == sizeof(Stale<int>),
              "Stale<HotPath<T>> must equal sizeof(Stale<T>): the band is byte-equivalent to T at the "
              "inner layer, so Stale's storage shape is unchanged.");
static_assert(fa::GradedWrapper<StaleOverHot>);

using TaggedRecipeSpec = Tagged<RecipeSpec<int>, FromUser>;
static_assert(sizeof(TaggedRecipeSpec) == sizeof(RecipeSpec<int>));

using WaitOverRecipeSpec = wait::SpinPause<RecipeSpec<int>>;
static_assert(sizeof(WaitOverRecipeSpec) == sizeof(RecipeSpec<int>));
static_assert(IsBand<WaitOverRecipeSpec>);

// Depth is the point of the cells that follow.  Collapse at two layers
// does not imply collapse at any depth, so the stack adds one band at a
// time until every band is in it.  Both orders of a pair are pinned
// where the pair is what a consumer writes.
using TwoBands = hot_path::Hot<det_safe::Pure<int>>;
static_assert(sizeof(TwoBands) == sizeof(int));
static_assert(IsBandOf<HotPathLattice, TwoBands>);

using TwoBandsReversed = det_safe::Pure<hot_path::Hot<int>>;
static_assert(sizeof(TwoBandsReversed) == sizeof(int));
static_assert(IsBandOf<DetSafeLattice, TwoBandsReversed>);

using ThreeBands = hot_path::Hot<det_safe::Pure<numerical_tier::Bitexact<int>>>;
static_assert(sizeof(ThreeBands) == sizeof(int));
static_assert(IsBand<ThreeBands>);

using ThreeHotsAtTop = hot_path::Hot<cipher_tier::Hot<alloc_class::Arena<int>>>;
static_assert(sizeof(ThreeHotsAtTop) == sizeof(int), "Three tier axes that spell structurally identical "
                                                     "lattices must compose orthogonally.  If this fires, "
                                                     "cross-lattice identity collapsed.");
static_assert(IsBand<ThreeHotsAtTop>);

using SevenBands = hot_path::Hot<
    wait::SpinPause<alloc_class::Arena<cipher_tier::Hot<opaque_lifetime::PerFleet<det_safe::Pure<numerical_tier::Bitexact<int>>>>>>>;
static_assert(sizeof(SevenBands) == sizeof(int), "Every band stacked over one int must still cost one int: "
                                                 "seven regime-1 wrappers over seven distinct lattices.  If "
                                                 "this fires, one band stopped collapsing.");
static_assert(alignof(SevenBands) == alignof(int));
static_assert(IsBandOf<HotPathLattice, SevenBands>);

using SevenBandsOverStale = hot_path::Hot<
    wait::SpinPause<alloc_class::Arena<cipher_tier::Hot<opaque_lifetime::PerFleet<det_safe::Pure<numerical_tier::Bitexact<Stale<int>>>>>>>>;
static_assert(sizeof(SevenBandsOverStale) == sizeof(Stale<int>),
              "Seven bands over a Stale must collapse to the Stale: the one runtime grade at the root is "
              "the whole layout.");
static_assert(IsBand<SevenBandsOverStale>);

// Each band's satisfies_v reads the outer chain, so a stronger tier
// serves a weaker requirement and never the reverse.
static_assert(satisfies_v<det_safe::Pure<int>, DetSafeTier_v::PhiloxRng>);
static_assert(!satisfies_v<det_safe::PhiloxRng<int>, DetSafeTier_v::Pure>);
static_assert(satisfies_v<numerical_tier::Bitexact<int>, Tolerance::ULP_FP16>);
static_assert(!satisfies_v<numerical_tier::Fp16<int>, Tolerance::BITEXACT>);
static_assert(satisfies_v<opaque_lifetime::PerFleet<int>, Lifetime_v::PER_REQUEST>);
static_assert(!satisfies_v<opaque_lifetime::PerRequest<int>, Lifetime_v::PER_FLEET>);

// ── Smoke, one call per wrapper ─────────────────────────────────────
//
// The per-wrapper tests cover this behavior in depth.  These calls
// repeat one of each to confirm that the wrappers still behave the same
// way when one translation unit compiles all of them together, and so
// that this file has a runtime body at all.

[[nodiscard]] bool smoke_linear() {
    Linear<int> x = mint_linear<int>(42);
    if (x.peek() != 42) return false;
    return std::move(x).consume() == 42;
}

[[nodiscard]] bool smoke_affine() {
    Affine<int> x = mint_affine<int>(7);
    return x.peek() == 7;
}

[[nodiscard]] bool smoke_refined() {
    Refined<positive_local, int> r = mint_refined<positive_local>(7);
    if (r.value() != 7) return false;
    return std::move(r).into() == 7;
}

[[nodiscard]] bool smoke_sealed_refined() {
    SealedRefined<positive_local, int> s = mint_sealed_refined<positive_local>(42);
    if (s.value() != 42) return false;
    // Sealing removes into(), so a sealed value is reached only through
    // value(); the second door is construction from a Refined.
    Refined<positive_local, int> r = mint_refined<positive_local>(7);
    SealedRefined<positive_local, int> sealed_from_r{std::move(r)};
    return sealed_from_r.value() == 7;
}

[[nodiscard]] bool smoke_tagged() {
    Tagged<int, FromUser> t = mint_tagged<FromUser>(99);
    if (t.value() != 99) return false;
    return std::move(t).into() == 99;
}

[[nodiscard]] bool smoke_secret() {
    Secret<int> s = mint_secret<int>(0xCAFE);
    // declassify accepts only a policy from the admitted roster; a
    // local struct is rejected at compile time in test_secret.
    return std::move(s).template declassify<tags::secret_policy::HashForCompare>() == 0xCAFE;
}

[[nodiscard]] bool smoke_monotonic() {
    Monotonic<std::uint64_t> m = mint_monotonic<std::uint64_t>(10);
    if (m.get() != 10) return false;
    m.advance(20);
    if (m.get() != 20) return false;
    if (!m.try_advance(20)) return false;  // leq holds for equal
    return !m.try_advance(15);  // backward fails
}

[[nodiscard]] bool smoke_append_only() {
    AppendOnly<int> a = mint_append_only<int>();
    a.append(1);
    a.append(2);
    a.append(3);
    if (a.size() != 3) return false;
    return a[0] == 1 && a[1] == 2 && a[2] == 3;
}

[[nodiscard]] bool smoke_stale() {
    Stale<int> fresh = Stale<int>::fresh(42);
    if (fresh.peek() != 42) return false;
    Stale<int> older = Stale<int>::at(42, 3);
    if (older.staleness() == fresh.staleness()) return false;
    return std::move(older).consume() == 42;
}

[[nodiscard]] bool smoke_bands() {
    det_safe::Pure<int> pure{42, {}};
    if (pure.peek() != 42) return false;
    auto philox = relax<DetSafeTier_v::PhiloxRng>(pure);  // const& form copies
    if (philox.peek() != 42 || tier_of(philox) != DetSafeTier_v::PhiloxRng) return false;
    auto mono = relax<DetSafeTier_v::MonotonicClockRead>(std::move(philox));  // rvalue form moves
    if (mono.peek() != 42) return false;

    numerical_tier::Bitexact<int> bitexact{7, {}};
    auto fp16 = relax<Tolerance::ULP_FP16>(std::move(bitexact));
    if (fp16.peek() != 7 || tier_of(fp16) != Tolerance::ULP_FP16) return false;

    TaggedBitexact tagged = mint_tagged<FromUser>(numerical_tier::Bitexact<int>{77, {}});
    return std::move(tagged).into().peek() == 77;
}

[[nodiscard]] bool smoke_recipe_spec() {
    RecipeSpec<int> spec{7, {Tolerance::ULP_FP16, RecipeFamily::Kahan}};
    if (spec.peek() != 7) return false;
    if (!admits(spec, Tolerance::ULP_FP8, RecipeFamily::Kahan)) return false;
    return !admits(spec, Tolerance::BITEXACT, RecipeFamily::Kahan);
}

}  // namespace

int main() {
    struct Leg {
        const char* name;
        bool (*run)();
    };
    constexpr Leg legs[] = {
        {"linear", smoke_linear},
        {"affine", smoke_affine},
        {"refined", smoke_refined},
        {"sealed_refined", smoke_sealed_refined},
        {"tagged", smoke_tagged},
        {"secret", smoke_secret},
        {"monotonic", smoke_monotonic},
        {"append_only", smoke_append_only},
        {"stale", smoke_stale},
        {"bands", smoke_bands},
        {"recipe_spec", smoke_recipe_spec},
    };
    std::fprintf(stderr, "test_wrapper_verification:\n");
    int failed = 0;
    for (const Leg& leg : legs) {
        const bool ok = leg.run();
        std::fprintf(stderr, "  %-16s %s\n", leg.name, ok ? "OK" : "FAILED");
        if (!ok) ++failed;
    }
    if (failed != 0) return EXIT_FAILURE;
    std::fprintf(stderr, "\nALL PASSED: 17 fixy wrappers verified uniformly (9 classes, 7 bands, RecipeSpec)\n");
    return EXIT_SUCCESS;
}
