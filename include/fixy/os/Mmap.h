#pragma once

// The mapping syscalls: mmap, the anonymous form, and madvise.
//
// An atom pack says what the mapping is — its protection, its share
// mode, whether an executable page is licensed — and the pack is read
// twice.  Once for the region type and its modifiers, from which
// OwnedMmap::mint_region calculates the PROT_* and MAP_* bits, and once
// for the effect row the calling context has to admit.
//
// Design notes:
//
//  1. The atoms fixy::atom::mmap::{with_prot, with_share, trusted_jit}
//     carry the axis and the row.  The context gate reads the row off
//     the pack instead of naming IO and Block by hand.  The two answers
//     agree, and a pin in the check file of this header says so.  They
//     stop agreeing the moment
//     an atom lifts to something else, and the derived one is the one
//     that stays right.
//
//  2. No atom carries an advice tag: advise takes an Advice directly.
//     The tags themselves are declared in fixy/atoms/Os.h beside the
//     other nine tag namespaces, so the one walk there covers all ten.
//     Their MADV_* values are here, and so is the only clause that walk
//     cannot express: that every tag declared there has one.
//
//  3. The Permission argument on advise_release_aware is what admits
//     the advice that discards pages.  The gate compares the tag and the
//     brand of the permission with the mapping's own, so a permission of
//     another region does not admit a discard of this mapping.
//
//  4. A mapping carries a brand.  mint_mmap and mint_mmap_anon read the
//     tag and the brand of the exclusive permission the caller presents,
//     so a mapping has an identity past its tag.

#include <fixy/OwnedMmap.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
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

// The advice that discards the contents of the pages is handled apart
// from the rest.  DontNeed drops the pages at once, and Free lets the
// kernel drop them when memory is short, before the next write.  A read
// of a dropped page gives zeros, so each of the two races each reader of
// the region.  The plain advise surface refuses both, and the
// release-aware one takes a witness.
//
// The advice tags are declared in fixy/atoms/Os.h with the other nine tag
// namespaces.  What is here is what only this header can express: the
// MADV_* value of each tag, and the walk that asks each declared tag for
// a row.
//
// The PROT_* and MAP_* tables are in fixy/OwnedMmap.h, beside the one
// door that maps, because that door calculates its bits from the type of
// the region that it builds.

// The MADV_* value of each advice tag.  A tag reaches madvise only
// through a row of this table, and fixy/os/AtomPack.h says why a table
// is closed.  A class template map takes a specialization for a class of
// the caller, and the plain surface can then discard pages under a tag
// that it does not know.
inline constexpr ::fixy::atom_pack::tag_row<int> advice_table[] = {
    {^^advice::HugePage, MADV_HUGEPAGE},     {^^advice::NoHugePage, MADV_NOHUGEPAGE},
    {^^advice::Collapse, MADV_COLLAPSE},     {^^advice::Sequential, MADV_SEQUENTIAL},
    {^^advice::Random, MADV_RANDOM},         {^^advice::WillNeed, MADV_WILLNEED},
    {^^advice::DontNeed, MADV_DONTNEED},     {^^advice::Free, MADV_FREE},
    {^^advice::WipeOnFork, MADV_WIPEONFORK}, {^^advice::DontDump, MADV_DONTDUMP},
};

template <typename Advice>
concept KnownAdvice = ::fixy::atom_pack::has_row(advice_table, ^^Advice);

// The value of a tag that has a row.  The lookup is a function and not a
// variable template, for the reason fixy/OwnedMmap.h gives for its bits.
[[nodiscard]] consteval int advice_value_of(std::meta::info advice_tag) noexcept {
    return ::fixy::atom_pack::value_for(advice_table, advice_tag);
}

// Whether a MADV_* value lets the kernel discard the contents of the
// pages.  The rule reads the value and not the tag, so it holds for each
// row of the table.  MADV_REMOVE frees the backing store as well.
[[nodiscard]] consteval bool discards_pages(int madvise_value) noexcept {
    return madvise_value == MADV_DONTNEED || madvise_value == MADV_FREE || madvise_value == MADV_REMOVE;
}

template <typename Advice>
concept DiscardsPages = KnownAdvice<Advice> && discards_pages(advice_value_of(^^Advice));

namespace detail {

namespace atom_mmap = ::fixy::atom::mmap;
namespace eff = ::foundation::effects;

template <typename A>
inline constexpr bool is_with_prot_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::mmap::with_prot>;

template <typename A>
inline constexpr bool is_with_share_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::mmap::with_share>;

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
    is_with_share_v<A> && PrimaryShare<extract_share_t<std::remove_cvref_t<A>>>;

// An atom of another kind is not a prot or share atom, so it answers
// true here and the fold below is a conjunction over the whole pack.
template <typename A>
inline constexpr bool prot_atom_is_known_v = !is_with_prot_v<A> || MappedProt<extract_prot_t<std::remove_cvref_t<A>>>;

template <typename A>
inline constexpr bool share_atom_is_known_v =
    !is_with_share_v<A> || MappedShare<extract_share_t<std::remove_cvref_t<A>>>;

template <typename... Atoms>
inline constexpr bool all_atom_tags_known_v =
    ((prot_atom_is_known_v<Atoms> && share_atom_is_known_v<Atoms>) && ... && true);

// A pack names exactly one primary share atom.  The other share atoms are
// modifiers, and any number of them stack on the primary.
template <typename... Atoms>
concept HasOnePrimaryShareAtom = ((std::size_t{is_primary_with_share_v<Atoms>} + ... + std::size_t{0}) == 1);

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
    requires(!PrimaryShare<Share>)
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

}  // namespace detail

// Mapping and unmapping can both park the caller — on page-cache
// pressure, on a NUMA-remote page fault, on write-back — so the atoms
// lift to IO and Block.  The first clause reads the row off the pack, so
// an atom whose row is wider tightens the gate rather than passing
// through a check written before it existed.
//
// The last clause is the gate of OwnedMmap::mint_region, read over the
// region type and the modifiers of the pack.  So the licence of an
// executable page and the row of a mapping are one rule, stated once, in
// fixy/OwnedMmap.h.
template <typename Ctx, typename... Atoms>
concept CtxFitsMmapMint =
    ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
    && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot, Atoms...>
    && detail::HasOnePrimaryShareAtom<Atoms...>
    && detail::region_mint_admits_v<Ctx, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>,
                                    detail::region_modifiers_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsAnonMmapMint = CtxFitsMmapMint<Ctx, Atoms...> && detail::pack_has_anonymous_v<Atoms...>;

// The advice tags lift nothing, so these two gates name the row the
// madvise call itself needs rather than deriving it.  Both calls reach
// the same syscall as the mints above and can park for the same
// reasons.
//
// An advice tag is not an effect declaration.  It selects which madvise
// the call makes.  A pack that reaches the mints above can carry a
// with atom, whose effects fold into the required row and widen it.
template <typename Ctx>
concept CtxAdmitsAdvise =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

template <typename Ctx, typename Advice>
concept CtxFitsSafeAdvise = CtxAdmitsAdvise<Ctx> && KnownAdvice<Advice> && !DiscardsPages<Advice>;

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
concept CtxFitsReleaseAwareAdvise = CtxAdmitsAdvise<Ctx> && DiscardsPages<Advice> && ProofNamesMappingTag<Proof, Region>
                                 && MappingIsBranded<Region> && ProofNamesMappingBrand<Proof, Region>;

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
    auto region =
        detail::mint_region_with_<Region>(detail::region_modifiers_t<Atoms...>{}, ctx, owner, fd, length, offset);
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
    if (::madvise(region.data(), region.size(), advice_value_of(^^Advice))
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
    if (::madvise(region.data(), region.size(), advice_value_of(^^Advice))
        < 0) {  // SYSCALL-CAP-OK: advise_release_aware ctx-gate (CtxFitsReleaseAwareAdvise: IO+Block + the Permission of this mapping)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

}  // namespace fixy::mmap

namespace fixy::mmap::detail::mmap_surface_invariants {

// These four checks stay in this header, and the other checks are in its
// check file.  Each one calls the lookup of a closed table or a closed
// list that a gate of this header reads, in each translation unit that
// includes this header.  fixy/os/AtomPack.h says why that call must come
// before the code of the includer.
static_assert(PrimaryShare<share::Private>);

// Every tag fixy::mmap::prot and fixy::mmap::share declare has a row in
// its table.  The known-tag concepts read the tables, so this walk is the
// check that no declared tag is missing from them.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::mmap::prot,
                                                        [](std::meta::info prot_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(prot_table, prot_tag);
                                                        }>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::prot has no row in prot_table, so the gate "
              "refuses every mapping that names it.");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::mmap::share,
                                                        [](std::meta::info share_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(share_table, share_tag);
                                                        }>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::share has no row in share_table, so the gate "
              "refuses every mapping that names it.");

// The shape of every class in fixy::mmap::advice — empty, final, not an
// atom — is checked by the one walk in fixy/atoms/Os.h, which covers all
// ten tag namespaces.  What that walk cannot check is the clause below:
// the advice table is here, so only here can a walk ask whether every tag
// has a row.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::mmap::advice,
                                                        [](std::meta::info advice_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(advice_table, advice_tag);
                                                        }>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::advice has no row in advice_table, so every gate "
              "refuses it.  Add its value to the table, or delete the tag from fixy/atoms/Os.h.");

// The count of tags in this namespace is pinned with the other nine in
// fixy/atoms/Os.h, because a walk cannot notice a tag that was deleted and
// one total covers all ten.

}  // namespace fixy::mmap::detail::mmap_surface_invariants
