#pragma once

// The consumed band wrappers, as spellings over Graded.  A band pins one
// tier of a chain lattice in the type of a value: DetSafe<Pure, T> is
// the Absolute carrier over DetSafeLattice::At<Pure>.  The grade is the
// pinned singleton, so the carrier costs sizeof(T) and the tier is read
// from the type alone.
//
// Old spellings: include/crucible/safety/{DetSafe,AllocClass,HotPath,
// CipherTier,Wait,NumericalTier,OpaqueLifetime,RecipeSpec}.h, each a
// class of its own around the same Graded.  Only what a consumer calls
// survives here: the carrier's own peek and consume, the construction
// door, the admission query satisfies_v, the tier query, and relax.
//
// The old wrappers hid the substrate's weaken because on the outer chain
// it moves up, which would let a value claim a tier its source did not
// earn.  A band keeps that discipline for free: its grade is a
// one-element lattice, so weaken cannot change the tier at all.  The
// one operation that changes the tier is relax, which rebinds the type
// and moves down the chain only.
//
// A band is constructed with the substrate's two-argument constructor,
// Band{value, {}}: the second argument is the pinned singleton and the
// braces are its whole witness.  Band::at_bottom(value) is the same door
// read from the substrate side, because bottom and top coincide on a
// singleton.
//
// RecipeSpec is not a band.  It carries both numerical axes at run time,
// so the grade is stored beside the value and the caller decides
// admission with admits().

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>

#include <concepts>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// The enum spellings the old wrappers exported beside themselves.
using DetSafeTier_v = ::foundation::algebra::lattices::DetSafeTier;
using AllocClassTag_v = ::foundation::algebra::lattices::AllocClassTag;
using HotPathTier_v = ::foundation::algebra::lattices::HotPathTier;
using CipherTierTag_v = ::foundation::algebra::lattices::CipherTierTag;
using WaitStrategy_v = ::foundation::algebra::lattices::WaitStrategy;
using Lifetime_v = ::foundation::algebra::lattices::Lifetime;
using ::foundation::algebra::lattices::RecipeFamily;
using ::foundation::algebra::lattices::Tolerance;

using ::foundation::algebra::lattices::AllocClassLattice;
using ::foundation::algebra::lattices::CipherTierLattice;
using ::foundation::algebra::lattices::DetSafeLattice;
using ::foundation::algebra::lattices::HotPathLattice;
using ::foundation::algebra::lattices::LifetimeLattice;
using ::foundation::algebra::lattices::RecipeFamilyLattice;
using ::foundation::algebra::lattices::ToleranceLattice;
using ::foundation::algebra::lattices::WaitLattice;

// ── The generic band surface ────────────────────────────────────────
//
// Written once over every Graded<Absolute, L::At<v>, T>.  A per-band
// detector, tier query or relax would repeat the same five lines eight
// times, so none exists.

// A pinned grade: the At<v> of a lattice over a scoped enum.
template <typename L>
concept IsPinnedGrade = requires {
    typename L::outer_lattice;
    typename L::enum_type;
    typename L::element_type;
    requires std::is_scoped_enum_v<typename L::enum_type>;
    requires std::same_as<std::remove_const_t<decltype(L::pinned)>, typename L::enum_type>;
    requires std::is_empty_v<typename L::element_type>;
};

// A band: the Absolute carrier over a pinned grade.  Top-level cv and
// reference are stripped, as every recognition trait in the project
// does.
template <typename B>
concept IsBand = ::foundation::algebra::IsGraded<B>
              && (std::remove_cvref_t<B>::modality == ::foundation::algebra::ModalityKind::Absolute)
              && IsPinnedGrade<typename std::remove_cvref_t<B>::lattice_type>;

template <typename B>
inline constexpr bool is_band_v = IsBand<B>;

// The band over one outer lattice.  This is the query that replaces the
// old per-band detectors is_det_safe_v, is_hot_path_v and their kin:
// is_band_of_v<DetSafeLattice, B> asks what is_det_safe_v<B> asked.
template <typename L, typename B>
concept IsBandOf = IsBand<B> && std::same_as<typename std::remove_cvref_t<B>::lattice_type::outer_lattice, L>;

template <typename L, typename B>
inline constexpr bool is_band_of_v = IsBandOf<L, B>;

template <IsBand B>
using band_value_t = typename std::remove_cvref_t<B>::value_type;

template <IsBand B>
using band_lattice_t = typename std::remove_cvref_t<B>::lattice_type::outer_lattice;

template <IsBand B>
using band_tier_t = typename std::remove_cvref_t<B>::lattice_type::enum_type;

// The tier a band pins, read from the type.
template <IsBand B>
inline constexpr band_tier_t<B> band_tier_v = std::remove_cvref_t<B>::lattice_type::pinned;

// The same tier, read from a value.  The argument is read for its type
// alone.
template <IsBand B>
[[nodiscard]] constexpr band_tier_t<B> tier_of(B const&) noexcept {
    return band_tier_v<B>;
}

// The band that pins another tier of the same outer lattice.
template <typename B, auto NewTier>
    requires IsBand<B> && std::same_as<decltype(NewTier), band_tier_t<B>>
using rebind_band_t = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute,
                                                    typename band_lattice_t<B>::template At<NewTier>, band_value_t<B>>;

// The admission query.  A band at tier t satisfies a consumer that
// requires r exactly when r sits at or below t in the outer chain, so a
// stronger provider serves a weaker requirement and never the reverse.
template <typename B, auto Required>
    requires IsBand<B> && std::same_as<decltype(Required), band_tier_t<B>>
inline constexpr bool satisfies_v = band_lattice_t<B>::leq(Required, band_tier_v<B>);

// relax moves a value down the chain and never up.  The requires clause
// is the whole gate: a target above the pinned tier is a substitution
// failure at the call site.  The const& form copies and so asks for a
// copy-constructible payload, which moves the failure for a move-only T
// out to overload resolution; the rvalue form moves.
template <auto WeakerTier, IsBand B>
    requires std::same_as<decltype(WeakerTier), band_tier_t<B>> && (band_lattice_t<B>::leq(WeakerTier, band_tier_v<B>))
          && std::copy_constructible<band_value_t<B>>
[[nodiscard]] constexpr rebind_band_t<B, WeakerTier>
relax(B const& band) noexcept(std::is_nothrow_copy_constructible_v<band_value_t<B>>) {
    return rebind_band_t<B, WeakerTier>{band.peek(), {}};
}

template <auto WeakerTier, IsBand B>
    requires std::same_as<decltype(WeakerTier), band_tier_t<B>> && (band_lattice_t<B>::leq(WeakerTier, band_tier_v<B>))
          && (!std::is_lvalue_reference_v<B>)
[[nodiscard]] constexpr rebind_band_t<B, WeakerTier>
relax(B&& band) noexcept(std::is_nothrow_move_constructible_v<band_value_t<B>>) {
    return rebind_band_t<B, WeakerTier>{std::move(band).consume(), {}};
}

// ── The bands ───────────────────────────────────────────────────────

// How deterministic the source of the bytes is.  Stronger replay-safety
// is higher.
template <DetSafeTier_v Tier, class T>
using DetSafe =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, DetSafeLattice::At<Tier>, T>;

// Which allocation strategy produced the storage.  Cheaper is higher.
template <AllocClassTag_v Tag, class T>
using AllocClass =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, AllocClassLattice::At<Tag>, T>;

// Which work budget produced the value.  The tighter budget is higher.
template <HotPathTier_v Tier, class T>
using HotPath =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, HotPathLattice::At<Tier>, T>;

// Where the persisted bytes live.  Faster recovery is higher.
template <CipherTierTag_v Tier, class T>
using CipherTier =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, CipherTierLattice::At<Tier>, T>;

// How the producer waited for a cross-thread event.  Cheaper is higher.
template <WaitStrategy_v Strategy, class T>
using Wait = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, WaitLattice::At<Strategy>, T>;

// How far the computation can deviate from an exact result.  Tighter is
// higher.  There is no deduction from a value: every site names the
// tier, or it would acquire one it never stated.
template <Tolerance T_at, class T>
using NumericalTier =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, ToleranceLattice::At<T_at>, T>;

// The lifetime the value was produced under.  The wider scope is
// higher: a fleet-scoped value is available inside any single request.
// relax narrows the scope; nothing widens it, because a request-scoped
// value dies with the request.
template <Lifetime_v Scope, class T>
using OpaqueLifetime =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, LifetimeLattice::At<Scope>, T>;

namespace det_safe {
template <typename T>
using Pure = DetSafe<DetSafeTier_v::Pure, T>;
template <typename T>
using PhiloxRng = DetSafe<DetSafeTier_v::PhiloxRng, T>;
template <typename T>
using MonoClock = DetSafe<DetSafeTier_v::MonotonicClockRead, T>;
template <typename T>
using WallClock = DetSafe<DetSafeTier_v::WallClockRead, T>;
template <typename T>
using EntropyRead = DetSafe<DetSafeTier_v::EntropyRead, T>;
template <typename T>
using FsMtime = DetSafe<DetSafeTier_v::FilesystemMtime, T>;
template <typename T>
using NDS = DetSafe<DetSafeTier_v::NonDeterministicSyscall, T>;
}  // namespace det_safe

namespace alloc_class {
template <typename T>
using Stack = AllocClass<AllocClassTag_v::Stack, T>;
template <typename T>
using Pool = AllocClass<AllocClassTag_v::Pool, T>;
template <typename T>
using Arena = AllocClass<AllocClassTag_v::Arena, T>;
template <typename T>
using Heap = AllocClass<AllocClassTag_v::Heap, T>;
template <typename T>
using Mmap = AllocClass<AllocClassTag_v::Mmap, T>;
template <typename T>
using HugePage = AllocClass<AllocClassTag_v::HugePage, T>;
}  // namespace alloc_class

namespace hot_path {
template <typename T>
using Hot = HotPath<HotPathTier_v::Hot, T>;
template <typename T>
using Warm = HotPath<HotPathTier_v::Warm, T>;
template <typename T>
using Cold = HotPath<HotPathTier_v::Cold, T>;
}  // namespace hot_path

namespace cipher_tier {
template <typename T>
using Hot = CipherTier<CipherTierTag_v::Hot, T>;
template <typename T>
using Warm = CipherTier<CipherTierTag_v::Warm, T>;
template <typename T>
using Cold = CipherTier<CipherTierTag_v::Cold, T>;
}  // namespace cipher_tier

namespace wait {
template <typename T>
using SpinPause = Wait<WaitStrategy_v::SpinPause, T>;
template <typename T>
using BoundedSpin = Wait<WaitStrategy_v::BoundedSpin, T>;
template <typename T>
using UmwaitC01 = Wait<WaitStrategy_v::UmwaitC01, T>;
template <typename T>
using AcquireWait = Wait<WaitStrategy_v::AcquireWait, T>;
template <typename T>
using Park = Wait<WaitStrategy_v::Park, T>;
template <typename T>
using Block = Wait<WaitStrategy_v::Block, T>;
}  // namespace wait

namespace numerical_tier {
template <typename T>
using Relaxed = NumericalTier<Tolerance::RELAXED, T>;
template <typename T>
using Int8 = NumericalTier<Tolerance::ULP_INT8, T>;
template <typename T>
using Fp8 = NumericalTier<Tolerance::ULP_FP8, T>;
template <typename T>
using Fp16 = NumericalTier<Tolerance::ULP_FP16, T>;
template <typename T>
using Fp32 = NumericalTier<Tolerance::ULP_FP32, T>;
template <typename T>
using Fp64 = NumericalTier<Tolerance::ULP_FP64, T>;
template <typename T>
using Bitexact = NumericalTier<Tolerance::BITEXACT, T>;
}  // namespace numerical_tier

namespace opaque_lifetime {
template <typename T>
using PerRequest = OpaqueLifetime<Lifetime_v::PER_REQUEST, T>;
template <typename T>
using PerProgram = OpaqueLifetime<Lifetime_v::PER_PROGRAM, T>;
template <typename T>
using PerFleet = OpaqueLifetime<Lifetime_v::PER_FLEET, T>;
}  // namespace opaque_lifetime

// Nothing checked that the seven alias namespaces above cover their
// enums.  An enumerator added to a lattice enum and not given an alias
// here is simply unreachable by the short spelling, silently, and the
// aliases cannot be generated because GCC 16 has no code injection.
// What can be done is to notice, so the walk below does.
//
// It reads each namespace for alias templates, instantiates each one at
// a probe type, and reads the tier back off the band.  That is the only
// route: an alias template is not a type until it is substituted, so
// the tier it names cannot be read from the declaration alone.
namespace detail {

// Any complete type does.  The walk reads the tier off the band and
// never the payload.
struct alias_probe {};

// True when some alias template declared in Ns names Tier.
template <std::meta::info Ns, auto Tier>
[[nodiscard]] consteval bool some_alias_names_tier() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_template(member) && std::meta::can_substitute(member, {^^alias_probe})) {
            using B = [:std::meta::substitute(member, {^^alias_probe}):];
            if constexpr (IsBand<B> && std::same_as<band_tier_t<B>, decltype(Tier)>) {
                if constexpr (band_tier_v<B> == Tier) return true;
            }
        }
    }
#pragma GCC diagnostic pop
    return false;
}

// Every enumerator of the enum reflected by EnumInfo has an alias in
// Ns.  EnumInfo is dealiased by the caller, because the enum spellings
// this header exports are using-declarations and a reflection of one
// is not a reflection of the enum.
template <std::meta::info Ns, std::meta::info EnumInfo>
[[nodiscard]] consteval bool every_tier_has_an_alias() noexcept {
    static constexpr auto tiers = std::define_static_array(std::meta::enumerators_of(EnumInfo));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : tiers) {
        if constexpr (!some_alias_names_tier<Ns, ([:en:])>()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

// ── RecipeSpec ──────────────────────────────────────────────────────
//
// A value paired with the numerical strategy it was produced under: a
// tolerance tier and a reduction family, both carried at run time.  The
// tiers form a chain; the families do not, so two named families are
// incomparable siblings under a wildcard that stands for all of them.
// Construction is RecipeSpec<T>{value, {tier, family}}.

using RecipeSpecLattice = ::foundation::algebra::lattices::ProductLattice<ToleranceLattice, RecipeFamilyLattice>;

template <class T>
using RecipeSpec = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, RecipeSpecLattice, T>;

// The detection surface of the old IsRecipeSpec.h: the Absolute carrier
// over the product lattice, whatever the payload.
template <typename S>
concept IsRecipeSpec = ::foundation::algebra::IsGraded<S>
                    && (std::remove_cvref_t<S>::modality == ::foundation::algebra::ModalityKind::Absolute)
                    && std::same_as<typename std::remove_cvref_t<S>::lattice_type, RecipeSpecLattice>;

template <typename S>
inline constexpr bool is_recipe_spec_v = IsRecipeSpec<S>;

template <IsRecipeSpec S>
using recipe_spec_value_t = typename std::remove_cvref_t<S>::value_type;

template <class T>
[[nodiscard]] constexpr Tolerance tolerance_of(RecipeSpec<T> const& spec) noexcept {
    return spec.grade().first;
}

template <class T>
[[nodiscard]] constexpr RecipeFamily recipe_family_of(RecipeSpec<T> const& spec) noexcept {
    return spec.grade().second;
}

// The direction is request below claim on each axis: a request is
// admitted when the value's claim covers it, not the reverse.
template <class T>
[[nodiscard]] constexpr bool admits(RecipeSpec<T> const& spec, Tolerance req_tier, RecipeFamily req_family) noexcept {
    return ToleranceLattice::leq(req_tier, tolerance_of(spec))
        && RecipeFamilyLattice::leq(req_family, recipe_family_of(spec));
}

// ── Self-test ───────────────────────────────────────────────────────

namespace detail::bands_self_test {

using PureInt = DetSafe<DetSafeTier_v::Pure, int>;
using PhiloxInt = DetSafe<DetSafeTier_v::PhiloxRng, int>;
using MonoInt = DetSafe<DetSafeTier_v::MonotonicClockRead, int>;
using NdsInt = DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>;

static_assert(sizeof(PureInt) == sizeof(int));
static_assert(sizeof(hot_path::Hot<double>) == sizeof(double));
static_assert(sizeof(RecipeSpec<int>) >= sizeof(int) + 2);

static_assert(IsBand<PureInt>);
static_assert(IsBand<PureInt const&>);
static_assert(IsBand<opaque_lifetime::PerFleet<int>>);
static_assert(!IsBand<int>);
static_assert(!IsBand<RecipeSpec<int>>, "the grade of a RecipeSpec is stored, not pinned");

static_assert(IsBandOf<DetSafeLattice, PureInt>);
static_assert(!IsBandOf<HotPathLattice, PureInt>);
static_assert(is_band_of_v<LifetimeLattice, opaque_lifetime::PerRequest<int>>);

static_assert(std::is_same_v<band_value_t<PureInt>, int>);
static_assert(std::is_same_v<band_lattice_t<PureInt>, DetSafeLattice>);
static_assert(std::is_same_v<band_tier_t<PureInt>, DetSafeTier_v>);
static_assert(band_tier_v<PureInt> == DetSafeTier_v::Pure);
static_assert(band_tier_v<PhiloxInt const&> == DetSafeTier_v::PhiloxRng);

static_assert(satisfies_v<PureInt, DetSafeTier_v::PhiloxRng>);
static_assert(satisfies_v<PhiloxInt, DetSafeTier_v::PhiloxRng>);
static_assert(!satisfies_v<MonoInt, DetSafeTier_v::PhiloxRng>,
              "MonotonicClockRead must not satisfy PhiloxRng.  A clock read "
              "does not reproduce on replay, so it must not reach a consumer "
              "that requires a value which does.");
static_assert(!satisfies_v<NdsInt, DetSafeTier_v::FilesystemMtime>);

static_assert(std::is_same_v<rebind_band_t<PureInt, DetSafeTier_v::PhiloxRng>, PhiloxInt>);

template <typename B, auto Target>
concept can_relax = requires(B b) { relax<Target>(std::move(b)); };

static_assert(can_relax<PureInt, DetSafeTier_v::PhiloxRng>);
static_assert(can_relax<PhiloxInt, DetSafeTier_v::PhiloxRng>);
static_assert(!can_relax<PhiloxInt, DetSafeTier_v::Pure>,
              "relax<Pure> on a PhiloxRng value must be rejected.  It would "
              "claim a determinism the source does not provide.");

constexpr PureInt pinned_pure{42, {}};
static_assert(pinned_pure.peek() == 42);
static_assert(tier_of(pinned_pure) == DetSafeTier_v::Pure);
static_assert(relax<DetSafeTier_v::PhiloxRng>(pinned_pure).peek() == 42);
static_assert(tier_of(relax<DetSafeTier_v::NonDeterministicSyscall>(pinned_pure))
              == DetSafeTier_v::NonDeterministicSyscall);

constexpr RecipeSpec<int> spec{7, {Tolerance::ULP_FP16, RecipeFamily::Kahan}};
static_assert(tolerance_of(spec) == Tolerance::ULP_FP16);
static_assert(recipe_family_of(spec) == RecipeFamily::Kahan);
static_assert(admits(spec, Tolerance::ULP_FP8, RecipeFamily::Kahan));
static_assert(admits(spec, Tolerance::ULP_FP16, RecipeFamily::None));
static_assert(!admits(spec, Tolerance::BITEXACT, RecipeFamily::Kahan));
static_assert(!admits(spec, Tolerance::ULP_FP16, RecipeFamily::Pairwise));

// Every enumerator of each lattice enum has a short spelling in the
// namespace that mirrors it.  An enumerator added to one of these enums
// and left without an alias is reachable only through the long
// Band<Tier, T> form, which is the gap these seven lines close.
static_assert(every_tier_has_an_alias<^^::fixy::det_safe, std::meta::dealias(^^DetSafeTier_v)>(),
              "fixy/Bands.h: a DetSafeTier enumerator has no alias in fixy::det_safe.");
static_assert(every_tier_has_an_alias<^^::fixy::alloc_class, std::meta::dealias(^^AllocClassTag_v)>(),
              "fixy/Bands.h: an AllocClassTag enumerator has no alias in fixy::alloc_class.");
static_assert(every_tier_has_an_alias<^^::fixy::hot_path, std::meta::dealias(^^HotPathTier_v)>(),
              "fixy/Bands.h: a HotPathTier enumerator has no alias in fixy::hot_path.");
static_assert(every_tier_has_an_alias<^^::fixy::cipher_tier, std::meta::dealias(^^CipherTierTag_v)>(),
              "fixy/Bands.h: a CipherTierTag enumerator has no alias in fixy::cipher_tier.");
static_assert(every_tier_has_an_alias<^^::fixy::wait, std::meta::dealias(^^WaitStrategy_v)>(),
              "fixy/Bands.h: a WaitStrategy enumerator has no alias in fixy::wait.");
// Tolerance arrives here through a using-declaration rather than an
// alias declaration, and `^^` cannot be applied to one, so this names
// the enum where it is defined.
static_assert(every_tier_has_an_alias<^^::fixy::numerical_tier, ^^::foundation::algebra::lattices::Tolerance>(),
              "fixy/Bands.h: a Tolerance enumerator has no alias in fixy::numerical_tier.");
static_assert(every_tier_has_an_alias<^^::fixy::opaque_lifetime, std::meta::dealias(^^Lifetime_v)>(),
              "fixy/Bands.h: a Lifetime enumerator has no alias in fixy::opaque_lifetime.");

// The walk answers no when an enumerator has no alias, which is what
// keeps the seven assertions above from passing vacuously.  det_safe
// holds no HotPathTier alias, so asking it about one is the shape of
// the failure without planting a defect in the table.
static_assert(!some_alias_names_tier<^^::fixy::det_safe, HotPathTier_v::Hot>(),
              "fixy/Bands.h: the alias walk must answer no for a tier the namespace does not name, or "
              "every_tier_has_an_alias proves nothing.");

}  // namespace detail::bands_self_test

}  // namespace fixy
