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
//  1. The atoms fixy::atom::mmap::{with_prot, with_share, trusted_jit,
//     faults_on_truncate, unchanged_file} carry the axis and the row.  The
//     context gate reads the row off the pack instead of naming IO and Block
//     by hand.  The two answers agree, and a pin in the check file of this
//     header says so.  They stop agreeing the moment an atom lifts to
//     something else, and the derived one is the one that stays right.
//
//  2. No atom carries an advice tag: advise takes an Advice directly.
//     The tags themselves are declared in fixy/atoms/Os.h beside the
//     other nine tag namespaces, so the one walk there covers all ten.
//     Their MADV_* values are here, and so is the only clause that walk
//     cannot express: that every tag declared there has one.
//
//  3. No door of this header discards pages.  An advice that discards
//     pages is a door of LockedMmap (fixy/os/LockedMmap.h), which holds the
//     mapping and the pool of its readers in one object.
//
//  4. A mapping carries a brand.  mint_mmap and mint_mmap_anon read the
//     tag and the brand of the exclusive permission the caller presents,
//     so a mapping has an identity past its tag.  The brand names the mint
//     site of the permission, and not one mapping.

#include <fixy/os/OwnedMmap.h>
#include <fixy/os/Fd.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy::mmap {

// The advice that discards the contents of the pages is handled apart
// from the rest.  DontNeed drops the pages at once, and Free lets the
// kernel drop them when memory is short, before the next write.  A read
// of a dropped page gives zeros, so each of the two races each reader of
// the region.  advise refuses both, and the release-aware door of LockedMmap
// (fixy/os/LockedMmap.h) gives them, under the exclusive of its own pool.
//
// The advice tags are declared in fixy/atoms/Os.h with the other nine tag
// namespaces.  What is here is what only this header can express: the
// MADV_* value of each tag, and the walk that asks each declared tag for
// a row.
//
// The PROT_* and MAP_* tables are in fixy/os/OwnedMmap.h, beside the one
// door that maps, because that door calculates its bits from the type of
// the region that it builds.

// The MADV_* value of each advice tag, with the values of
// foundation/os/MapWords.h.  A tag reaches madvise only through a row of
// this table, and fixy/os/AtomPack.h says why a table is closed.  A class
// template map takes a specialization for a class of the caller, and the
// plain surface can then discard pages under a tag that it does not know.
inline constexpr ::fixy::atom_pack::tag_row<int> advice_table[] = {
    {^^advice::HugePage, ::foundation::os::kAdviseHugePage},
    {^^advice::NoHugePage, ::foundation::os::kAdviseNoHugePage},
    {^^advice::Collapse, ::foundation::os::kAdviseCollapse},
    {^^advice::Sequential, ::foundation::os::kAdviseSequential},
    {^^advice::Random, ::foundation::os::kAdviseRandom},
    {^^advice::WillNeed, ::foundation::os::kAdviseWillNeed},
    {^^advice::DontNeed, ::foundation::os::kAdviseDontNeed},
    {^^advice::Free, ::foundation::os::kAdviseFree},
    {^^advice::WipeOnFork, ::foundation::os::kAdviseWipeOnFork},
    {^^advice::DontDump, ::foundation::os::kAdviseDontDump},
};

template <typename Advice>
concept KnownAdvice = ::fixy::atom_pack::has_row(advice_table, ^^Advice);

// The value of a tag that has a row.  The lookup is a function and not a
// variable template, for the reason fixy/os/OwnedMmap.h gives for its bits.
[[nodiscard]] consteval int advice_value_of(std::meta::info advice_tag) noexcept {
    return ::fixy::atom_pack::value_for(advice_table, advice_tag);
}

// Whether a MADV_* value lets the kernel discard the contents of the
// pages.  The rule reads the value and not the tag, so it holds for each
// row of the table.  MADV_REMOVE frees the backing store as well.
[[nodiscard]] consteval bool discards_pages(int madvise_value) noexcept {
    return madvise_value == ::foundation::os::kAdviseDontNeed || madvise_value == ::foundation::os::kAdviseFree
        || madvise_value == ::foundation::os::kAdviseRemove;
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
// flags that stack on the primary, and the licences.  The prot
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
template <typename... Modifiers, RegionLicence Licence>
struct push_modifier<region_modifiers<Modifiers...>, Licence> {
    using type = region_modifiers<Modifiers..., Licence>;
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

// The region of a map through the door form of OwnedMmap::mint_region that
// the source arguments name: the length alone for a map of no file, or the
// borrow of a file, the length and the offset.
template <typename Region, typename... Modifiers, typename Ctx, typename Owner, typename... Source>
[[nodiscard]] ::foundation::core::Result<Region, ::foundation::os::Errno>
mint_region_with_(region_modifiers<Modifiers...>, Ctx const& ctx, Owner const& owner, Source... source) noexcept {
    return Region::template mint_region<Modifiers...>(ctx, owner, source...);
}

// The region of a map of a descriptor that a C library opened, over the
// modifier list of a pack.
template <typename Region, typename... Modifiers, typename Ctx, typename Owner>
[[nodiscard]] ::foundation::core::Result<Region, ::foundation::os::Errno>
map_foreign_region_with_(region_modifiers<Modifiers...>, Ctx const& ctx, Owner const& owner,
                         ForeignDescriptor descriptor, std::size_t length, ::fixy::fs::FileOffset offset) noexcept {
    return Region::template map_foreign_region<Modifiers...>(ctx, owner, descriptor, length, offset);
}

// The gate of the map of a file, read over the modifier list of a pack, as
// region_mint_admits_v reads the gate of a region.
template <typename Ctx, typename Prot, typename Share, typename Kind, ::fixy::os::RightSet Held, typename List>
inline constexpr bool file_region_mint_admits_v = false;
template <typename Ctx, typename Prot, typename Share, typename Kind, ::fixy::os::RightSet Held, typename... Modifiers>
inline constexpr bool file_region_mint_admits_v<Ctx, Prot, Share, Kind, Held, region_modifiers<Modifiers...>> =
    CtxFitsFileRegionMint<Ctx, Prot, Share, Kind, Held, Modifiers...>;

// The Linear of the region of a map, or the error of the map.  The region
// holds an address, so the step takes it through CRUCIBLE_TRY and not a
// match, which gives no result that holds an address.
template <typename Region>
[[nodiscard]] ::foundation::core::Result<Linear<Region>, ::foundation::os::Errno>
linear_region_(::foundation::core::Result<Region, ::foundation::os::Errno> mapped) noexcept {
    CRUCIBLE_TRY(region, std::move(mapped));
    return mint_linear<Region>(std::move(region));
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
// fixy/os/OwnedMmap.h.
template <typename Ctx, typename... Atoms>
concept CtxFitsMmapMint =
    ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
    && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::mmap::with_prot, Atoms...>
    && detail::HasOnePrimaryShareAtom<Atoms...>
    && detail::region_mint_admits_v<Ctx, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>,
                                    detail::region_modifiers_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsAnonMmapMint = CtxFitsMmapMint<Ctx, Atoms...> && detail::pack_has_anonymous_v<Atoms...>;

// The gate of the map of a file through a handle of the kind Kind with the
// rights Held: the gate of the pack, no anonymous share, the two rules of the
// end of a shared file, the rule of the private map of a file, and the rights
// that file_map_rights of fixy/os/OwnedMmap.h names.  The last clause is the
// gate of the region door, and the three clauses before it name the rule of a
// refusal.
template <typename Ctx, typename Kind, ::fixy::os::RightSet Held, typename... Atoms>
concept CtxFitsFileMmapMint =
    CtxFitsMmapMint<Ctx, Atoms...> && !detail::pack_has_anonymous_v<Atoms...> && MappedFileKind<Kind>
    && SealedMapIsShared<Kind, detail::primary_share_of_t<Atoms...>>
    && SharedFileMapIsLicensed<Kind, detail::primary_share_of_t<Atoms...>, std::remove_cvref_t<Atoms>...>
    && PrivateFileMapIsLicensed<Kind, detail::primary_share_of_t<Atoms...>, std::remove_cvref_t<Atoms>...>
    && detail::file_region_mint_admits_v<Ctx, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Kind,
                                         Held, detail::region_modifiers_t<Atoms...>>;

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

// The mapping takes its tag and its brand from the exclusive permission
// the caller presents.  The mint reads the permission and does not
// consume it: the caller keeps it, and can park it in a pool.  Each mapping
// minted with one permission has the tag and the brand of that permission.
// The mapping that a discard can reach comes from the mints of LockedMmap
// (fixy/os/LockedMmap.h).
//
// The file is the borrow of the Handle of the file, with the right map,
// and also read and write for a shared map that writes (the source of a map
// of fixy/os/OwnedMmap.h).  A shared map takes sealed memory, or a regular
// file with the atom faults_on_truncate in the pack (the end of a shared
// file).  A private map takes a regular file with the atom unchanged_file (the
// private map of a file), and mint_file_copy is the form with no atom.  The map
// reads the file from the byte at offset.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Tag, typename Brand, class Kind,
          ::fixy::os::RightSet Rights>
    requires CtxFitsFileMmapMint<Ctx, Kind, Rights, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>>,
    ::foundation::os::Errno>
mint_mmap(Ctx const& ctx, ::foundation::permissions::Permission<Tag, Brand> const& owner,
          ::fixy::os::HandleRef<Kind, Rights> file, std::size_t length,
          ::fixy::fs::FileOffset offset = ::fixy::fs::FileOffset{0}) noexcept {
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>;
    // The syscall lives with the region's only constructor, in
    // fixy/os/OwnedMmap.h, so that no address but the kernel's can become a
    // region, and its bits come from the region type.  What this mint
    // adds is the atom pack: which atoms, and the row they lift to.
    return detail::linear_region_<Region>(
        detail::mint_region_with_<Region>(detail::region_modifiers_t<Atoms...>{}, ctx, owner, file, length, offset));
}

// A copy of length bytes of a regular file from the byte at offset, in
// read-only memory of its own (the private map of a file of
// fixy/os/OwnedMmap.h).  No later change of the file changes the copy, and no
// read of it faults past an end.  The handle holds the read and the seek right,
// because the reads are positioned, and they leave the position of the file.  A
// file that ends before the range gives enxio, and an empty handle gives ebadf.
// The permission is read as in mint_mmap.  Complexity: linear in length.
template <::foundation::effects::IsExecCtx Ctx, typename Tag, typename Brand, ::fixy::os::RightSet Rights>
    requires CtxFitsFileCopyMint<Ctx, prot::ReadOnly, share::Anonymous, Rights>
[[nodiscard]] inline ::foundation::core::Result<Linear<OwnedMmap<Tag, prot::ReadOnly, share::Anonymous, Brand>>,
                                                ::foundation::os::Errno>
mint_file_copy(Ctx const& ctx, ::foundation::permissions::Permission<Tag, Brand> const& owner,
               ::fixy::os::HandleRef<::fixy::os::kind::File, Rights> file, ::foundation::core::ByteCount length,
               ::fixy::fs::FileOffset offset = ::fixy::fs::FileOffset{0}) noexcept {
    using Region = OwnedMmap<Tag, prot::ReadOnly, share::Anonymous, Brand>;
    return detail::linear_region_<Region>(Region::mint_file_copy(ctx, owner, file, length, offset));
}

// An anonymous mapping maps no file.  The permission is read as in
// mint_mmap.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx, typename Tag, typename Brand>
    requires CtxFitsAnonMmapMint<Ctx, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>>,
    ::foundation::os::Errno>
mint_mmap_anon(Ctx const& ctx, ::foundation::permissions::Permission<Tag, Brand> const& owner,
               std::size_t length) noexcept {
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>, Brand>;
    return detail::linear_region_<Region>(
        detail::mint_region_with_<Region>(detail::region_modifiers_t<Atoms...>{}, ctx, owner, length));
}

// The advice gives the unit, or the error of the kernel.  The door gives
// einval for a region that is not mapped, and it makes no system call.  No
// advice of this door changes a byte of the region, so it takes a region
// that is const.  The region of a LockedMmap gives these advices to a reader.
template <typename Advice, typename Tag, typename Prot, typename Share, typename Brand,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSafeAdvise<Ctx, Advice>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
advise(Ctx const&, OwnedMmap<Tag, Prot, Share, Brand> const& region) noexcept {
    return detail::advise_region_(region.data(), region.size(), advice_value_of(^^Advice));
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
