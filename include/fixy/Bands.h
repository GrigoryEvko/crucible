#pragma once

// The consumed band wrappers, as spellings over Graded.  A band pins one
// tier of a chain lattice in the type of a value: DetSafe<Pure, T> is
// the Absolute carrier over DetSafeLattice::At<Pure>.  The grade is the
// pinned singleton, so the carrier costs sizeof(T) and the tier is read
// from the type alone.
//
// A band gives only what a consumer calls: the carrier's own peek and
// consume, the construction door, the admission query satisfies_v, the
// tier query, and relax.
//
// On the outer chain the substrate's weaken moves up, which would let a
// value claim a tier that its source did not earn.  The grade of a band
// is a one-element lattice, so weaken cannot change the tier at all.
// The one operation that changes the tier is relax, which rebinds the
// type and moves down the chain only.
//
// A band's tier is a claim about the bytes, and nothing checks it: a
// DetSafe<Pure, T> says a deterministic source produced the value.  So a
// band is built through one named door, mint_band<Band>(value), and a
// search for mint_band< lists every site that asserts a tier.  The
// substrate refuses the braces Band{value, {}} and a default value,
// because both would pair a value with the tier and name no authority.
// A class that builds a band through the substrate's keyed constructor
// names itself in a grade_key, which the same kind of search finds.
//
// RecipeSpec is not a band.  It carries both numerical axes at run time,
// so the grade is stored beside the value and the caller decides
// admission with admits().  Its door is mint_recipe_spec.  On each axis
// up is the stronger claim.  The stored grade is the order dual, and the
// weaken() and compose() of the substrate can only relax a claim.
//
// Two bands pin a partial order rather than a chain.  ScopedFence's
// scopes form two trunks that meet only at the ends, so two scopes on
// different trunks are incomparable and neither satisfies the other.
// Vendor's named backends sit side by side between None and Portable, so
// two different named backends are incomparable in the same way.
// Nothing in the generic surface below changes for them: every operation
// here is written in terms of the outer lattice's leq alone, and leq is
// defined on a partial order.  satisfies_v answers no for an
// incomparable pair, and relax rejects one, so a cross-trunk move is a
// substitution failure by the same clause that rejects a move up.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/ResidencyHeatLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// The enum of the tiers of each band, under the name that the band uses.
using DetSafeTier_v = ::foundation::algebra::lattices::DetSafeTier;
using AllocClassTag_v = ::foundation::algebra::lattices::AllocClassTag;
using HotPathTier_v = ::foundation::algebra::lattices::HotPathTier;
using CipherTierTag_v = ::foundation::algebra::lattices::CipherTierTag;
using WaitStrategy_v = ::foundation::algebra::lattices::WaitStrategy;
using Lifetime_v = ::foundation::algebra::lattices::Lifetime;
using MemoryScope_v = ::foundation::algebra::lattices::MemoryScope;
using VendorBackend_v = ::foundation::algebra::lattices::VendorBackend;
using ResidencyHeatTag_v = ::foundation::algebra::lattices::ResidencyHeatTag;
using ::foundation::algebra::lattices::RecipeFamily;
using ::foundation::algebra::lattices::Tolerance;

using ::foundation::algebra::lattices::AllocClassLattice;
using ::foundation::algebra::lattices::CipherTierLattice;
using ::foundation::algebra::lattices::DetSafeLattice;
using ::foundation::algebra::lattices::HotPathLattice;
using ::foundation::algebra::lattices::LifetimeLattice;
using ::foundation::algebra::lattices::MemoryScopeLattice;
using ::foundation::algebra::lattices::RecipeFamilyLattice;
using ::foundation::algebra::lattices::ResidencyHeatLattice;
using ::foundation::algebra::lattices::ToleranceLattice;
using ::foundation::algebra::lattices::VendorLattice;
using ::foundation::algebra::lattices::WaitLattice;

// ── The generic band surface ────────────────────────────────────────
//
// Written once over every Graded<Absolute, L::At<v>, T>.  A per-band
// detector, tier query or relax would repeat the same five lines for
// every band, so none exists.

// A pinned grade: the At<v> of a lattice over a scoped enum, whose outer
// order puts the stronger claim higher.  relax moves a band down, and
// satisfies_v admits a band at or above a requirement.  The two
// directions are sound only for that orientation.  A pinned grade of an
// outer lattice that states another orientation, or none, is not a band.
template <typename L>
concept IsPinnedGrade = requires {
    typename L::outer_lattice;
    typename L::enum_type;
    typename L::element_type;
    requires std::is_scoped_enum_v<typename L::enum_type>;
    requires std::same_as<std::remove_const_t<decltype(L::pinned)>, typename L::enum_type>;
    requires std::is_empty_v<typename L::element_type>;
    requires ::foundation::algebra::claim_orientation_v<typename L::outer_lattice>
                 == ::foundation::algebra::ClaimOrientation::stronger_is_higher;
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

// The band over one outer lattice.  One query serves each band:
// is_band_of_v<DetSafeLattice, B> asks whether B is a DetSafe band.
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

// A band spelled exactly, with no cv or reference.
template <typename B>
concept ExactBand = IsBand<B> && std::same_as<B, std::remove_cvref_t<B>>;

// The door through which a value takes a band's tier.  B names the band
// exactly, so the tier a site asserts is spelled at that site.  The key's
// authority is a class local to this function, which no other code can
// name.
template <ExactBand B>
[[nodiscard]] constexpr B
mint_band(band_value_t<B> value) noexcept(std::is_nothrow_move_constructible_v<band_value_t<B>>) {
    struct door {
        [[nodiscard]] static constexpr B
        open(band_value_t<B>&& held) noexcept(std::is_nothrow_move_constructible_v<band_value_t<B>>) {
            return B{::foundation::algebra::grade_key<door>{}, std::move(held), {}};
        }
    };
    return door::open(std::move(value));
}

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
    return mint_band<rebind_band_t<B, WeakerTier>>(band.peek());
}

template <auto WeakerTier, IsBand B>
    requires std::same_as<decltype(WeakerTier), band_tier_t<B>> && (band_lattice_t<B>::leq(WeakerTier, band_tier_v<B>))
          && (!std::is_lvalue_reference_v<B>)
[[nodiscard]] constexpr rebind_band_t<B, WeakerTier>
relax(B&& band) noexcept(std::is_nothrow_move_constructible_v<band_value_t<B>>) {
    return mint_band<rebind_band_t<B, WeakerTier>>(std::move(band).consume());
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

// The memory-visibility scope a publication was released under.  The
// scopes form a partial order over two trunks that meet only at the
// ends: Warp ⊑ Cta ⊑ Cluster ⊑ Gpu on the accelerator side, Inner ⊑
// Outer on the ARM shareability side, with a shared bottom Thread and a
// shared top System.  Scopes on different trunks are incomparable — a
// block scope has no ordering relation to an inner-shareable domain, so
// neither one satisfies the other.
//
// S is the scope the fence publishes at.  Wider visibility is higher, so
// a consumer requirement R is met when S subsumes R, and a device-wide
// fence meets a block-scope requirement while a block-scope fence does
// not meet a device-wide one.
//
// relax narrows the scope, and is sound because a wider fence really does
// publish at every narrower scope it dominates: a device-wide fence has
// already made the writes visible at block scope, so re-labelling narrows
// where the value is offered and never overstates what the fence covered.
// Relaxing up, or across to an incomparable trunk, is a compile error.
// It would assert the value is visible to observers the fence never
// reached.
template <MemoryScope_v S, class T>
using ScopedFence =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, MemoryScopeLattice::At<S>, T>;

// The backend that produced the value.  The order is partial: None is the
// bottom, Portable is the top, and the named backends between them are
// incomparable.  A portable value runs wherever a named one does, so
// Portable satisfies every requirement.  A named backend satisfies only
// itself and None, and two different named backends satisfy neither.
// relax narrows toward None.  A move up to Portable, or across to another
// named backend, is a compile error, because it would claim that the
// value runs on hardware its producer never built for.
template <VendorBackend_v Backend, class T>
using Vendor =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, VendorLattice::At<Backend>, T>;

// The cache level that holds the working set of the value.  Nearer the
// core is higher.  A Hot value serves a Warm requirement, because
// evicting it outward is always possible.  A Cold value does not serve a
// Hot one, because the consumer would pay a miss it did not budget for.
template <ResidencyHeatTag_v Tier, class T>
using ResidencyHeat =
    ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, ResidencyHeatLattice::At<Tier>, T>;

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

namespace scoped_fence {
template <typename T>
using Thread = ScopedFence<MemoryScope_v::Thread, T>;
template <typename T>
using Warp = ScopedFence<MemoryScope_v::Warp, T>;
template <typename T>
using Cta = ScopedFence<MemoryScope_v::Cta, T>;
template <typename T>
using Cluster = ScopedFence<MemoryScope_v::Cluster, T>;
template <typename T>
using Gpu = ScopedFence<MemoryScope_v::Gpu, T>;
template <typename T>
using Inner = ScopedFence<MemoryScope_v::Inner, T>;
template <typename T>
using Outer = ScopedFence<MemoryScope_v::Outer, T>;
template <typename T>
using System = ScopedFence<MemoryScope_v::System, T>;
}  // namespace scoped_fence

namespace vendor {
template <typename T>
using None = Vendor<VendorBackend_v::None, T>;
template <typename T>
using Cpu = Vendor<VendorBackend_v::CPU, T>;
template <typename T>
using Nv = Vendor<VendorBackend_v::NV, T>;
template <typename T>
using Amd = Vendor<VendorBackend_v::AMD, T>;
template <typename T>
using Tpu = Vendor<VendorBackend_v::TPU, T>;
template <typename T>
using Trn = Vendor<VendorBackend_v::TRN, T>;
template <typename T>
using Cer = Vendor<VendorBackend_v::CER, T>;
template <typename T>
using Portable = Vendor<VendorBackend_v::Portable, T>;
}  // namespace vendor

namespace residency_heat {
template <typename T>
using Cold = ResidencyHeat<ResidencyHeatTag_v::Cold, T>;
template <typename T>
using Warm = ResidencyHeat<ResidencyHeatTag_v::Warm, T>;
template <typename T>
using Hot = ResidencyHeat<ResidencyHeatTag_v::Hot, T>;
}  // namespace residency_heat

// Nothing checked that the alias namespaces above cover their
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

// Every enumerator of the enum reflected by EnumInfo has an alias in
// Ns.  EnumInfo is dealiased by the caller, because the enum spellings
// this header exports are using-declarations and a reflection of one
// is not a reflection of the enum.
//
// The walk expands the members of Ns one time.  Each alias template that
// gives a band of the enum marks each enumerator that holds its tier, in
// a plain loop.  Then each enumerator must hold a mark.  Complexity: the
// member count of Ns times the number of enumerators.
template <std::meta::info Ns, std::meta::info EnumInfo>
[[nodiscard]] consteval bool every_tier_has_an_alias() noexcept {
    using Enum = [:EnumInfo:];
    static constexpr auto tiers = std::define_static_array(std::meta::enumerators_of(EnumInfo));
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
    std::array<bool, tiers.size()> is_named{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_template(member) && std::meta::can_substitute(member, {^^alias_probe})) {
            using B = [:std::meta::substitute(member, {^^alias_probe}):];
            if constexpr (IsBand<B> && std::same_as<band_tier_t<B>, Enum>) {
                for (std::size_t index = 0; index < tiers.size(); ++index) {
                    if (std::meta::extract<Enum>(std::meta::constant_of(tiers[index])) == band_tier_v<B>) {
                        is_named[index] = true;
                    }
                }
            }
        }
    }
#pragma GCC diagnostic pop
    for (const bool tier_is_named : is_named) {
        if (!tier_is_named) return false;
    }
    return true;
}

}  // namespace detail

// ── RecipeSpec ──────────────────────────────────────────────────────
//
// A value paired with the numerical strategy it was produced under: a
// tolerance tier and a reduction family, both carried at run time.  The
// tiers form a chain.  The families do not: two named families are
// incomparable siblings under a wildcard that stands for all of them.
//
// A tighter tier and a wider family are the stronger claims, and the
// grade is the product of the two order duals.  weaken() moves toward
// RELAXED and None, and compose() gives the looser tier and the common
// family.

using RecipeSpecLattice =
    ::foundation::algebra::lattices::ProductLattice<::foundation::algebra::lattices::DualLattice<ToleranceLattice>,
                                                    ::foundation::algebra::lattices::DualLattice<RecipeFamilyLattice>>;

template <class T>
using RecipeSpec = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, RecipeSpecLattice, T>;

// A payload a RecipeSpec can hold: an object that moves into the carrier.
template <class T>
concept RecipeSpecPayload = std::is_object_v<T> && std::move_constructible<T>;

// The door through which a value takes a numerical strategy.  The two
// axes are a claim about how the value was produced, and nothing checks
// it, so it is made here by name, as a band's tier is made in mint_band.
template <RecipeSpecPayload T>
[[nodiscard]] constexpr RecipeSpec<T>
mint_recipe_spec(T value, Tolerance tier, RecipeFamily family) noexcept(std::is_nothrow_move_constructible_v<T>) {
    struct door {
        [[nodiscard]] static constexpr RecipeSpec<T>
        open(T&& held, Tolerance held_tier,
             RecipeFamily held_family) noexcept(std::is_nothrow_move_constructible_v<T>) {
            return RecipeSpec<T>{::foundation::algebra::grade_key<door>{}, std::move(held), {held_tier, held_family}};
        }
    };
    return door::open(std::move(value), tier, family);
}

// True when S is a RecipeSpec: the Absolute carrier over the product
// lattice, whatever the payload.
template <typename S>
concept IsRecipeSpec = ::foundation::algebra::IsGraded<S>
                    && (std::remove_cvref_t<S>::modality == ::foundation::algebra::ModalityKind::Absolute)
                    && std::same_as<typename std::remove_cvref_t<S>::lattice_type, RecipeSpecLattice>;

template <typename S>
inline constexpr bool is_recipe_spec_v = IsRecipeSpec<S>;

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

}  // namespace fixy
