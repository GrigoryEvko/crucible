#pragma once

// The mapping syscalls: mmap, the anonymous form, and madvise.
//
// An atom pack says what the mapping is — its protection, its share
// mode, whether an executable page is licensed — and the pack is read
// twice.  Once for the region type and its modifiers, from which
// OwnedMmap::mint_region calculates the PROT_* and MAP_* bits, and once
// for the effect row the calling context has to admit.
//
// Old spelling: include/crucible/fixy/Mmap.h.
//
// Deviations from that header, each deliberate:
//
//  1. Grants became atoms.  fixy::atom::mmap::{with_prot, with_share,
//     trusted_jit} carry the axis and the row, so the which_dim
//     specializations the old header wrote by hand are gone with the
//     grant system that needed them.
//
//  2. The context gate reads the row off the pack instead of naming IO
//     and Block by hand.  Both answers are the same today, and the pin
//     below says so.  They stop being the same the moment an atom lifts
//     to something else, and the derived one is the one that stays
//     right.
//
//  3. The old with_advice grant is gone, because nothing lifts an advice
//     tag: advise takes an Advice directly, never an atom, so the grant
//     was decoration in the same sense the sched grants were.  The tags
//     themselves are declared in fixy/atoms/Os.h beside the other nine
//     tag namespaces, so the one walk there covers all ten.  Their MADV_*
//     values stay here, and so does the only clause that walk cannot
//     express: that every tag declared there has one.
//
//  4. is_leak_grant is not here.  fixy/OwnedMmap.h already took that
//     witness to fixy::atom::IsLeakAtom, which is one reflection query
//     over the atom catalog rather than a trait any translation unit
//     could specialize.
//
//  5. The release_aware grant and its two predicates are dropped.  The
//     grant was dead in the old header: the concept was written
//     `CtxAdmitsIoBlock<Ctx> && is_dangerous_advice_v<Advice>` and named
//     neither the grant nor RegionTag, so the tag was a parameter the
//     gate never read.  The Permission argument on advise_release_aware
//     is what admits the dangerous advice.  The old header took it as
//     Permission<RegionTag> with RegionTag free, so a permission of any
//     region admitted a discard of any mapping.  Here the gate compares
//     the tag and the brand of the permission with the mapping's own.
//
//  6. A mapping carries a brand.  mint_mmap and mint_mmap_anon read the
//     tag and the brand of the exclusive permission the caller presents.
//     The old mints took the tag as a template argument, and a mapping
//     had no identity past its tag, so two mappings of one tag were one
//     type.

#include <fixy/OwnedMmap.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

// A libc header older than a flag does not declare it, so each value is
// spelled here as well.

#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25  // Linux 6.1 (2022-12).
#endif
#ifndef MADV_FREE
#define MADV_FREE 8  // Linux 4.5 (2016-03).
#endif
#ifndef MADV_WIPEONFORK
#define MADV_WIPEONFORK 18  // Linux 4.14 (2017-11).
#endif
#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 16  // Linux 3.4 (2012-05).
#endif

namespace fixy::mmap {

// DontNeed is the one that has to be handled apart from the rest: it
// zeros the pages, which races any concurrent reader of the region.  The
// plain advise surface refuses it and the release-aware one takes a
// witness.
//
// The advice tags are declared in fixy/atoms/Os.h with the other nine tag
// namespaces.  What is here is what only this header can express: each
// tag's MADV_* value, and the clause that every tag declared there has
// one.
//
// The PROT_* and MAP_* bit maps are in fixy/OwnedMmap.h, beside the one
// door that maps, because that door calculates its bits from the type of
// the region it builds.

template <typename Advice>
struct advice_value : std::integral_constant<int, -1> {};
template <>
struct advice_value<advice::HugePage> : std::integral_constant<int, MADV_HUGEPAGE> {};
template <>
struct advice_value<advice::NoHugePage> : std::integral_constant<int, MADV_NOHUGEPAGE> {};
template <>
struct advice_value<advice::Collapse> : std::integral_constant<int, MADV_COLLAPSE> {};
template <>
struct advice_value<advice::Sequential> : std::integral_constant<int, MADV_SEQUENTIAL> {};
template <>
struct advice_value<advice::Random> : std::integral_constant<int, MADV_RANDOM> {};
template <>
struct advice_value<advice::WillNeed> : std::integral_constant<int, MADV_WILLNEED> {};
template <>
struct advice_value<advice::DontNeed> : std::integral_constant<int, MADV_DONTNEED> {};
template <>
struct advice_value<advice::Free> : std::integral_constant<int, MADV_FREE> {};
template <>
struct advice_value<advice::WipeOnFork> : std::integral_constant<int, MADV_WIPEONFORK> {};
template <>
struct advice_value<advice::DontDump> : std::integral_constant<int, MADV_DONTDUMP> {};

template <typename Advice>
inline constexpr int advice_value_v = advice_value<Advice>::value;

// A named predicate rather than an inline comparison, because the set
// can grow: a kernel that starts discarding eagerly where it discards
// lazily today moves that advice into this set.

template <typename Advice>
inline constexpr bool is_dangerous_advice_v = std::is_same_v<Advice, advice::DontNeed>;

namespace detail {

namespace atom_mmap = ::fixy::atom::mmap;
namespace eff = ::foundation::effects;

template <typename A>
inline constexpr bool is_with_prot_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::mmap::with_prot>;

template <typename A>
inline constexpr bool is_with_share_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::mmap::with_share>;

// The tag an atom was given.  void where the atom is of another kind,
// which is what lets the fold below skip it.
template <typename A>
struct extract_prot {
    using type = void;
};
template <typename Prot>
struct extract_prot<atom_mmap::with_prot<Prot>> {
    using type = Prot;
};
template <typename A>
using extract_prot_t = typename extract_prot<A>::type;

template <typename A>
struct extract_share {
    using type = void;
};
template <typename Share>
struct extract_share<atom_mmap::with_share<Share>> {
    using type = Share;
};
template <typename A>
using extract_share_t = typename extract_share<A>::type;

template <typename A>
inline constexpr bool is_primary_with_share_v =
    is_with_share_v<A> && is_primary_share_v<extract_share_t<std::remove_cvref_t<A>>>;

// An atom of another kind is not a prot or share atom, so it answers
// true here and the fold below is a conjunction over the whole pack.
template <typename A>
inline constexpr bool prot_atom_is_known_v =
    !is_with_prot_v<A> || is_known_prot_v<extract_prot_t<std::remove_cvref_t<A>>>;

template <typename A>
inline constexpr bool share_atom_is_known_v =
    !is_with_share_v<A> || is_known_share_v<extract_share_t<std::remove_cvref_t<A>>>;

template <typename... Atoms>
inline constexpr bool all_atom_tags_known_v =
    ((prot_atom_is_known_v<Atoms> && share_atom_is_known_v<Atoms>) && ... && true);

template <typename... Atoms>
inline constexpr bool has_prot_atom_v = (is_with_prot_v<Atoms> || ...);

template <typename... Atoms>
inline constexpr bool has_primary_share_atom_v = (is_primary_with_share_v<Atoms> || ...);

template <typename... Atoms>
inline constexpr bool has_duplicate_prot_v = (static_cast<int>(is_with_prot_v<Atoms>) + ... + 0) > 1;

template <typename... Atoms>
inline constexpr bool has_duplicate_primary_share_v = (static_cast<int>(is_primary_with_share_v<Atoms>) + ... + 0) > 1;

template <typename... Atoms>
struct prot_of {
    using type = void;
};
template <typename First, typename... Rest>
struct prot_of<First, Rest...> {
    using type = std::conditional_t<is_with_prot_v<First>, extract_prot_t<First>, typename prot_of<Rest...>::type>;
};
template <typename... Atoms>
using prot_of_t = typename prot_of<Atoms...>::type;

template <typename... Atoms>
struct primary_share_of {
    using type = void;
};
template <typename First, typename... Rest>
struct primary_share_of<First, Rest...> {
    using type = std::conditional_t<is_primary_with_share_v<First>, extract_share_t<First>,
                                    typename primary_share_of<Rest...>::type>;
};
template <typename... Atoms>
using primary_share_of_t = typename primary_share_of<Atoms...>::type;

// The modifiers a pack names for OwnedMmap::mint_region: the share
// flags that stack on the primary, and the trusted_jit licence.  The prot
// atom and the primary share atom become the type of the region, and an
// atom of another kind adds no bit, so the fold skips all three.
template <typename... Modifiers>
struct region_modifiers {};

template <typename List, typename A>
struct push_modifier {
    using type = List;
};
template <typename... Modifiers, typename Share>
    requires(!is_primary_share_v<Share>)
struct push_modifier<region_modifiers<Modifiers...>, atom_mmap::with_share<Share>> {
    using type = region_modifiers<Modifiers..., Share>;
};
template <typename... Modifiers>
struct push_modifier<region_modifiers<Modifiers...>, atom_mmap::trusted_jit> {
    using type = region_modifiers<Modifiers..., atom_mmap::trusted_jit>;
};

template <typename List, typename... Atoms>
struct fold_modifiers {
    using type = List;
};
template <typename List, typename First, typename... Rest>
struct fold_modifiers<List, First, Rest...>
    : fold_modifiers<typename push_modifier<List, std::remove_cvref_t<First>>::type, Rest...> {};

template <typename... Atoms>
using region_modifiers_t = typename fold_modifiers<region_modifiers<>, Atoms...>::type;

// The gate of the region door, read over the modifier list of a pack.  A
// pack that names no prot or no primary share gives void, and the door
// refuses void.
template <typename Ctx, typename Prot, typename Share, typename List>
inline constexpr bool region_mint_admits_v = false;
template <typename Ctx, typename Prot, typename Share, typename... Modifiers>
inline constexpr bool region_mint_admits_v<Ctx, Prot, Share, region_modifiers<Modifiers...>> =
    CtxFitsRegionMint<Ctx, Prot, Share, Modifiers...>;

template <typename Region, typename... Modifiers, typename Ctx, typename Owner>
[[nodiscard]] std::expected<Region, std::error_code> mint_region_with_(region_modifiers<Modifiers...>, Ctx const& ctx,
                                                                       Owner const& owner, int fd, std::size_t length,
                                                                       ::off_t offset) noexcept {
    return Region::template mint_region<Modifiers...>(ctx, owner, fd, length, offset);
}

template <typename A>
inline constexpr bool is_anonymous_share_v =
    std::is_same_v<std::remove_cvref_t<A>, atom_mmap::with_share<share::Anonymous>>;

template <typename... Atoms>
inline constexpr bool pack_has_anonymous_v = (is_anonymous_share_v<Atoms> || ...);

// The row a pack exercises is the union of the rows its atoms lift to.
// The old header named IO and Block at the gate; this reads them off
// the pack, so an atom whose row is wider tightens the gate rather than
// passing through a check written before it existed.
template <typename... Atoms>
struct atoms_row {
    using type = eff::Row<>;
};
template <typename First, typename... Rest>
struct atoms_row<First, Rest...> {
    using type = eff::row_union_t<eff::lift_row_t<std::remove_cvref_t<First>>, typename atoms_row<Rest...>::type>;
};
template <typename... Atoms>
using atoms_row_t = typename atoms_row<Atoms...>::type;

}  // namespace detail

// Mapping and unmapping can both park the caller — on page-cache
// pressure, on a NUMA-remote page fault, on write-back — so the atoms
// lift to IO and Block, and this is what makes the context carry them.
template <typename Ctx, typename... Atoms>
concept CtxAdmitsAtomRow = ::foundation::effects::IsExecCtx<Ctx>
                        && (::fixy::atom::IsAtom<std::remove_cvref_t<Atoms>> && ...)
                        && (::foundation::effects::LiftsToRow<std::remove_cvref_t<Atoms>> && ...)
                        && ::foundation::effects::CtxAdmits<Ctx, detail::atoms_row_t<Atoms...>>;

// The last clause is the gate of OwnedMmap::mint_region, read over the
// region type and the modifiers of the pack.  So the licence of an
// executable page and the row of a mapping are one rule, stated once, in
// fixy/OwnedMmap.h.
template <typename Ctx, typename... Atoms>
concept CtxFitsMmapMint = CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
                       && detail::has_prot_atom_v<Atoms...> && detail::has_primary_share_atom_v<Atoms...>
                       && !detail::has_duplicate_prot_v<Atoms...> && !detail::has_duplicate_primary_share_v<Atoms...>
                       && detail::region_mint_admits_v<Ctx, detail::prot_of_t<Atoms...>,
                                                       detail::primary_share_of_t<Atoms...>,
                                                       detail::region_modifiers_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsAnonMmapMint = CtxFitsMmapMint<Ctx, Atoms...> && detail::pack_has_anonymous_v<Atoms...>;

// The advice tags lift nothing, so these two gates name the row the
// madvise call itself needs rather than deriving it.  Both calls reach
// the same syscall as the mints above and can park for the same
// reasons.
//
// Still true after atom::with<Es...> gained its lift: an advice tag is
// not an effect declaration, it selects which madvise the call makes.
// What changed is that a pack reaching the mints above may now carry a
// with atom, whose effects fold into the required row and widen it.
template <typename Ctx>
concept CtxAdmitsAdvise =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

template <typename Ctx, typename Advice>
concept CtxFitsSafeAdvise = CtxAdmitsAdvise<Ctx> && (advice_value_v<Advice> >= 0) && !is_dangerous_advice_v<Advice>;

// A release names the mapping it acts on, by the tag and the brand of the
// mapping.  Each clause is its own concept, so a refusal names the clause
// that failed.
//
// The proof is compared as the type the caller passed.  A parameter
// spelled Permission<Tag, Brand> would also deduce from a class derived
// from a permission, and IsPermissionFor refuses that class, because it
// matches the Permission template exactly.  A share, a guard, and a
// permission of a tag derived from the mapping's tag are each refused as
// another type.
template <typename Proof, typename Region>
concept ProofNamesMappingTag = ::foundation::permissions::IsPermissionFor<Proof, typename Region::tag_type>;

// On the erased brand every mapping of a tag is one type, so a proof of
// one of them would stand in for all of them.  Only a mint with a
// permission of a fresh brand gives a branded mapping.
template <typename Region>
concept MappingIsBranded = ::foundation::brand::IsBranded<Region>;

template <typename Proof, typename Region>
concept ProofNamesMappingBrand = ::foundation::brand::SameBrand<Proof, Region>;

template <typename Ctx, typename Advice, typename Region, typename Proof>
concept CtxFitsReleaseAwareAdvise =
    CtxAdmitsAdvise<Ctx> && (advice_value_v<Advice> >= 0) && is_dangerous_advice_v<Advice>
    && ProofNamesMappingTag<Proof, Region> && MappingIsBranded<Region> && ProofNamesMappingBrand<Proof, Region>;

// The mapping takes its tag and its brand from the exclusive permission
// the caller presents.  The mint reads the permission and does not
// consume it: the caller keeps it, can park it in a pool, and presents
// it again to advise_release_aware.  Each mapping minted with one
// permission has the identity of that permission, and the exclusive of
// that identity covers each of them.
//
// The descriptor is a plain int rather than an owning handle type, so
// that a descriptor from any source reaches this without a conversion.
//
// §XXI carve-out: cx=alloc — mapping is a kernel side effect.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Tag, typename Brand>
    requires CtxFitsMmapMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>>, std::error_code>
mint_mmap(Ctx const& ctx, ::foundation::permissions::Permission<Tag, Brand> const& owner, int fd, std::size_t length,
          ::off_t offset = 0) noexcept {
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>;
    // The syscall lives with the region's only constructor, in
    // fixy/OwnedMmap.h, so that no address but the kernel's can become a
    // region, and its bits come from the region type.  What this mint
    // adds is the atom pack: which atoms, and the row they lift to.
    auto region = detail::mint_region_with_<Region>(detail::region_modifiers_t<Atoms...>{}, ctx, owner, fd, length,
                                                    offset);
    if (!region) {
        return std::unexpected{region.error()};
    }
    return mint_linear<Region>(std::move(*region));
}

// An anonymous mapping takes a descriptor of -1 and an offset of 0,
// which is the convention mmap(2) states.  The permission is read as in
// mint_mmap.
//
// §XXI carve-out: cx=alloc — mapping is a kernel side effect.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Tag, typename Brand>
    requires CtxFitsAnonMmapMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>>, std::error_code>
mint_mmap_anon(Ctx const& ctx, ::foundation::permissions::Permission<Tag, Brand> const& owner,
               std::size_t length) noexcept {
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>;
    auto region = detail::mint_region_with_<Region>(detail::region_modifiers_t<Atoms...>{}, ctx, owner, -1, length, 0);
    if (!region) {
        return std::unexpected{region.error()};
    }
    return mint_linear<Region>(std::move(*region));
}

template <typename Advice, typename Tag, typename Prot, typename Share, typename Brand,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSafeAdvise<Ctx, Advice>
[[nodiscard]] inline std::expected<void, std::error_code> advise(Ctx const&,
                                                                 OwnedMmap<Tag, Prot, Share, Brand>& region) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>)
        < 0) {  // SYSCALL-CAP-OK: advise ctx-gate (CtxFitsSafeAdvise: CtxAdmitsAdvise, effects::IO+Block)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// What makes this surface safe is the permission the caller has to
// produce.  An exclusive permission over a region is obtainable only
// once every outstanding share of it has been deposited back, so holding
// one witnesses that no reader is live.  The permission is move-only, so
// the caller cannot have handed it to a reader between obtaining it and
// arriving here.
//
// The gate admits only the permission of this mapping: its tag is the
// mapping's tag and its brand is the mapping's brand.  The mapping took
// both from the permission it was minted with, so the proof and the
// mapping name one identity, and a permission of another region cannot
// stand in.  The Advice argument is the only one a caller spells.  The
// rest are deduced, and a spelled tag that differs from the mapping's
// tag does not bind the region.
//
// A brand names a mint site, not a mint call, per fact 2 of
// foundation/Brand.h.  Two permissions minted by one statement in a loop
// are one type, so two mappings minted with them are one type too, and
// the gate cannot tell them apart.  A permission carries no state at run
// time, so there is no identity here to compare at run time either.
// Mint the two permissions at two sites when both mappings must live at
// the same time.
//
// It is borrowed rather than consumed because discarding pages leaves
// the region usable, so the caller keeps it and may hand it back out
// afterwards.
template <typename Advice, typename Tag, typename Prot, typename Share, typename Brand, typename Proof,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsReleaseAwareAdvise<Ctx, Advice, OwnedMmap<Tag, Prot, Share, Brand>, Proof>
[[nodiscard]] inline std::expected<void, std::error_code>
advise_release_aware(Ctx const&, OwnedMmap<Tag, Prot, Share, Brand>& region,
                     Proof const& /*exclusive_proof*/) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>)
        < 0) {  // SYSCALL-CAP-OK: advise_release_aware ctx-gate (CtxFitsReleaseAwareAdvise: IO+Block + the Permission of this mapping)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

}  // namespace fixy::mmap

namespace fixy::mmap::detail::mmap_surface_invariants {

static_assert(advice_value_v<advice::DontNeed> == MADV_DONTNEED);
static_assert(advice_value_v<advice::HugePage> == MADV_HUGEPAGE);
static_assert(advice_value_v<advice::Free> == MADV_FREE);

static_assert(is_dangerous_advice_v<advice::DontNeed>);
static_assert(!is_dangerous_advice_v<advice::HugePage>);
static_assert(!is_dangerous_advice_v<advice::Sequential>);

static_assert(is_primary_share_v<share::Private>);
static_assert(is_primary_share_v<share::Shared>);
static_assert(is_primary_share_v<share::Anonymous>);
static_assert(!is_primary_share_v<share::Locked>);
static_assert(!is_primary_share_v<share::Populate>);
static_assert(!is_primary_share_v<share::HugeTLB>);

using A_RO = ::fixy::atom::mmap::with_prot<prot::ReadOnly>;
using A_Shared = ::fixy::atom::mmap::with_share<share::Shared>;
using A_Private = ::fixy::atom::mmap::with_share<share::Private>;
using A_Anon = ::fixy::atom::mmap::with_share<share::Anonymous>;
using A_Locked = ::fixy::atom::mmap::with_share<share::Locked>;
using A_Exec = ::fixy::atom::mmap::with_prot<prot::Exec>;
using A_Jit = ::fixy::atom::mmap::trusted_jit;

static_assert(has_prot_atom_v<A_RO, A_Shared>);
static_assert(!has_prot_atom_v<A_Shared>);
static_assert(has_primary_share_atom_v<A_RO, A_Shared>);
static_assert(has_primary_share_atom_v<A_RO, A_Anon>);
static_assert(!has_primary_share_atom_v<A_RO, A_Locked>);
static_assert(has_duplicate_prot_v<A_RO, A_RO>);
static_assert(!has_duplicate_prot_v<A_RO, A_Shared>);
static_assert(has_duplicate_primary_share_v<A_Shared, A_Private>);
static_assert(!has_duplicate_primary_share_v<A_Shared, A_Locked>);
static_assert(pack_has_anonymous_v<A_RO, A_Anon>);
static_assert(!pack_has_anonymous_v<A_RO, A_Shared>);

// The empty pack answers false rather than failing to compile, which is
// what lets the mint gate reject it instead of hard-erroring.
static_assert(!has_prot_atom_v<>);
static_assert(!has_primary_share_atom_v<>);
static_assert(!has_duplicate_prot_v<>);

static_assert(std::is_same_v<prot_of_t<A_RO, A_Shared>, prot::ReadOnly>);
static_assert(std::is_same_v<prot_of_t<A_Shared, A_Exec, A_Jit>, prot::Exec>);
static_assert(std::is_same_v<primary_share_of_t<A_RO, A_Shared>, share::Shared>);
static_assert(std::is_same_v<primary_share_of_t<A_RO, A_Anon, A_Locked>, share::Anonymous>);

// The prot atom and the primary share atom become the region type.  The
// modifiers are what is left of the pack for the region door.
static_assert(std::is_same_v<region_modifiers_t<A_RO, A_Shared>, region_modifiers<>>);
static_assert(std::is_same_v<region_modifiers_t<A_Shared, A_Exec, A_Locked, A_Jit>,
                             region_modifiers<share::Locked, ::fixy::atom::mmap::trusted_jit>>);

// The derived row and the row the old header named by hand are the same
// answer.  This is the pin on that equality: it fails if an mmap atom
// stops lifting to IO and Block, which is a decision somebody has to
// make rather than discover.
using ExpectedMmapRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<atoms_row_t<A_RO, A_Shared>, ExpectedMmapRow>);
static_assert(std::is_same_v<atoms_row_t<A_Exec, A_Private, A_Jit>, ExpectedMmapRow>);
static_assert(std::is_same_v<atoms_row_t<>, eff::Row<>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;
using FgCtx = eff::ExecCtx<>;

static_assert(CtxFitsMmapMint<IoBlockCtx, A_RO, A_Shared>);
static_assert(!CtxFitsMmapMint<IoOnlyCtx, A_RO, A_Shared>,
              "a context without Block must not reach mmap: the call can park on page-cache pressure.");
static_assert(!CtxFitsMmapMint<FgCtx, A_RO, A_Shared>);

static_assert(CtxFitsAnonMmapMint<IoBlockCtx, A_RO, A_Anon>);
static_assert(!CtxFitsAnonMmapMint<IoBlockCtx, A_RO, A_Shared>);

static_assert(!CtxFitsMmapMint<IoBlockCtx, A_Exec, A_Private>,
              "an executable mapping needs the trusted_jit atom.");
static_assert(CtxFitsMmapMint<IoBlockCtx, A_Exec, A_Private, A_Jit>);

static_assert(!CtxFitsMmapMint<IoBlockCtx>, "an empty atom pack names no protection and no share mode.");

static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::HugePage>);
static_assert(CtxFitsSafeAdvise<IoBlockCtx, advice::Sequential>);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, advice::DontNeed>,
              "DontNeed zeroes the pages, so it must go through the release-aware door.");
static_assert(!CtxFitsSafeAdvise<IoOnlyCtx, advice::HugePage>);

// The release gate, read against one probe mapping and each proof a
// caller could hand it.  The brands are named types, so each claim reads
// without a mint.  The mapping spelled without a brand is on the erased
// identity.
struct ProbeRegion {};
struct ProbeOtherRegion final {};
struct ProbeDerivedRegion final : ProbeRegion {};
struct ProbeBrand final {};
struct ProbeOtherBrand final {};

using ProbeMapping = OwnedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous, ProbeBrand>;
using ErasedMapping = OwnedMmap<ProbeRegion, prot::WriteCopy, share::Anonymous>;

using OwnProof = ::foundation::permissions::Permission<ProbeRegion, ProbeBrand>;
using OtherTagProof = ::foundation::permissions::Permission<ProbeOtherRegion, ProbeBrand>;
using DerivedTagProof = ::foundation::permissions::Permission<ProbeDerivedRegion, ProbeBrand>;
using OtherBrandProof = ::foundation::permissions::Permission<ProbeRegion, ProbeOtherBrand>;
using ShareProof = ::foundation::permissions::SharedPermission<ProbeRegion, ProbeBrand>;
// A class derived from the right permission.  It has the tag_type and
// the brand_type of that permission, which is why the tag clause asks
// for the Permission template exactly rather than for the two members.
struct LookAlikeProof : OwnProof {};

static_assert(CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::DontNeed, ProbeMapping, OwnProof>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::HugePage, ProbeMapping, OwnProof>,
              "the release-aware door is for the dangerous advice only.  The rest go through advise.");
static_assert(!CtxFitsReleaseAwareAdvise<IoOnlyCtx, advice::DontNeed, ProbeMapping, OwnProof>);

static_assert(ProofNamesMappingTag<OwnProof, ProbeMapping> && MappingIsBranded<ProbeMapping>
              && ProofNamesMappingBrand<OwnProof, ProbeMapping>);
static_assert(!ProofNamesMappingTag<OtherTagProof, ProbeMapping>,
              "a permission of another region must not admit a discard of this mapping.");
static_assert(!ProofNamesMappingTag<DerivedTagProof, ProbeMapping>,
              "a tag derived from the mapping's tag is another tag.");
static_assert(!ProofNamesMappingTag<LookAlikeProof, ProbeMapping>, "a class derived from a permission is not one.");
static_assert(!ProofNamesMappingTag<ShareProof, ProbeMapping>, "a share is not the exclusive.");
static_assert(!ProofNamesMappingBrand<OtherBrandProof, ProbeMapping>,
              "a permission of another instance of the tag must not admit a discard of this mapping.");
static_assert(!MappingIsBranded<ErasedMapping>,
              "on the erased brand every mapping of the tag is one type, so no proof names one of them.");

// The same refusals through the function itself, where the tag and the
// brand are deduced from the mapping.
template <typename Mapping, typename Proof>
concept CanRelease = requires(IoBlockCtx const& ctx, Mapping& mapping, Proof const& proof) {
    advise_release_aware<advice::DontNeed>(ctx, mapping, proof);
};
static_assert(CanRelease<ProbeMapping, OwnProof>);
static_assert(!CanRelease<ProbeMapping, OtherTagProof>);
static_assert(!CanRelease<ProbeMapping, DerivedTagProof>);
static_assert(!CanRelease<ProbeMapping, LookAlikeProof>);
static_assert(!CanRelease<ProbeMapping, ShareProof>);
static_assert(!CanRelease<ProbeMapping, OtherBrandProof>);
static_assert(!CanRelease<ErasedMapping, OwnProof>);

// A tag with no entry in a bit map has no bits, and a read of them is a
// compile error.  These cells are the witness: an atom over a tag with
// no entry is refused at the gate, and it cannot fold into PROT_NONE.
struct NotAProt final {};
struct NotAShare final {};
using A_UnknownProt = ::fixy::atom::mmap::with_prot<NotAProt>;
using A_UnknownShare = ::fixy::atom::mmap::with_share<NotAShare>;

static_assert(!MappedProt<NotAProt>, "a prot tag with no entry must have no bits, not PROT_NONE.");
static_assert(!MappedShare<NotAShare>, "a share tag with no entry must have no bits, not zero.");
static_assert(!MappedProt<void> && !MappedShare<void>);
static_assert(MappedProt<prot::ReadOnly> && MappedShare<share::Private>);
static_assert(!is_known_prot_v<NotAProt>);
static_assert(!is_known_share_v<NotAShare>);
static_assert(all_atom_tags_known_v<A_RO, A_Shared>);
static_assert(!all_atom_tags_known_v<A_UnknownProt, A_Shared>);
static_assert(!all_atom_tags_known_v<A_RO, A_Shared, A_UnknownShare>);
static_assert(!CtxFitsMmapMint<IoBlockCtx, A_UnknownProt, A_Shared>,
              "a prot tag with no PROT_* mapping must be refused at the gate, not mapped as PROT_NONE.");

// Every tag fixy::mmap::prot and fixy::mmap::share declare has an entry
// in its bit map.  The known-tag predicates are the maps, so this walk
// is the check that no declared tag is missing from them.
template <std::meta::info Ns, bool IsProt>
[[nodiscard]] consteval bool every_tag_in_is_known_() noexcept {
    static constexpr auto members = std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if constexpr (IsProt) {
                if (!is_known_prot_v<T>) return false;
            } else {
                if (!is_known_share_v<T>) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_tag_in_is_known_<^^::fixy::mmap::prot, true>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::prot has no entry in prot_bits, so the gate "
              "refuses every mapping that names it.");
static_assert(every_tag_in_is_known_<^^::fixy::mmap::share, false>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::share has no entry in share_flags, so the "
              "gate refuses every mapping that names it.");

// A type that is not an advice tag has no value, and the gate reads
// that rather than instantiating madvise with -1.
struct NotAnAdvice final {};
static_assert(advice_value_v<NotAnAdvice> == -1);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, NotAnAdvice>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, NotAnAdvice, ProbeMapping, OwnProof>);

// The shape of every class in fixy::mmap::advice — empty, final, not an
// atom — is checked by the one walk in fixy/atoms/Os.h, which covers all
// ten tag namespaces.  What that walk cannot check is the clause below:
// advice_value_v is declared here, so only here can a walk ask whether
// every tag has a MADV_* value rather than the -1 sentinel.
[[nodiscard]] consteval bool every_advice_class_has_a_value_() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^::fixy::mmap::advice, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if (advice_value_v<T> < 0) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_advice_class_has_a_value_(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::advice has no MADV_* value, so advice_value_v "
              "answers the -1 sentinel for it and every gate refuses it.  Add its value to advice_value "
              "below, or delete the tag from fixy/atoms/Os.h.");

// The count of tags in this namespace is pinned with the other nine in
// fixy/atoms/Os.h, because a walk cannot notice a tag that was deleted and
// one total covers all ten.

}  // namespace fixy::mmap::detail::mmap_surface_invariants
