#pragma once

// The mapping syscalls: mmap, the anonymous form, and madvise.
//
// An atom pack says what the mapping is — its protection, its share
// mode, whether an executable page is licensed — and the pack is read
// twice.  Once for the PROT_* and MAP_* bits the syscall takes, and once
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
//  3. The advice tags live here rather than in fixy/atoms/Os.h, because
//     nothing lifts them: advise takes an Advice directly, never an
//     atom, so the old with_advice grant was decoration in the same
//     sense the sched grants were.  The walk at the foot of this header
//     is the same shape Os.h runs over its nine namespaces.  When the
//     owner of Os.h next touches it, ^^::fixy::mmap::advice belongs in
//     os_tag_namespaces so one walk covers all ten.
//
//  4. is_leak_grant is not here.  fixy/OwnedMmap.h already took that
//     witness to fixy::atom::IsLeakAtom, which is one reflection query
//     over the atom catalog rather than a trait any translation unit
//     could specialize.
//
//  5. The release_aware grant and its two predicates are dropped, and
//     CtxFitsReleaseAwareAdvise lost the RegionTag parameter it took.
//     The grant was dead in the old header: the concept was written
//     `CtxAdmitsIoBlock<Ctx> && is_dangerous_advice_v<Advice>` and named
//     neither the grant nor RegionTag, so the tag was a parameter the
//     gate never read.  What actually admits the dangerous advice is the
//     Permission argument on advise_release_aware, which is still there
//     and still tag-matched to the region.

#include <fixy/OwnedMmap.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
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
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << 26)
#endif

namespace fixy::mmap {

// DontNeed is the one that has to be handled apart from the rest: it
// zeros the pages, which races any concurrent reader of the region.  The
// plain advise surface refuses it and the release-aware one takes a
// witness.
//
// These tags are read by advise and by nothing else, so they are
// declared beside it.  The prot and share tags they sit next to belong
// to fixy/atoms/Os.h instead, because an atom takes those as arguments.

namespace advice {
struct HugePage final {};
struct NoHugePage final {};
struct Collapse final {};
struct Sequential final {};
struct Random final {};
struct WillNeed final {};
struct DontNeed final {};
struct Free final {};
struct WipeOnFork final {};
struct DontDump final {};
}  // namespace advice

// Write and execute are never both set: prot::Exec carries read and
// execute only.  A JIT that has to stage writes maps the same pages
// twice, once writable for code generation and once executable to run
// them, which is the discipline hardware execute-only memory assumes
// anyway.
//
// WriteCopy and ReadWrite carry identical bits and differ only in the
// share mode they are meant to accompany.

template <typename Prot>
struct prot_bits : std::integral_constant<int, 0> {};
template <>
struct prot_bits<prot::ReadOnly> : std::integral_constant<int, PROT_READ> {};
template <>
struct prot_bits<prot::WriteCopy> : std::integral_constant<int, PROT_READ | PROT_WRITE> {};
template <>
struct prot_bits<prot::ReadWrite> : std::integral_constant<int, PROT_READ | PROT_WRITE> {};
template <>
struct prot_bits<prot::Exec> : std::integral_constant<int, PROT_READ | PROT_EXEC> {};

template <typename Prot>
inline constexpr int prot_bits_v = prot_bits<Prot>::value;

// The first three are primary modes and a mapping has exactly one.  The
// last three are flags that stack on any primary.

template <typename Share>
struct share_flags : std::integral_constant<int, 0> {};
template <>
struct share_flags<share::Private> : std::integral_constant<int, MAP_PRIVATE> {};
template <>
struct share_flags<share::Shared> : std::integral_constant<int, MAP_SHARED> {};
template <>
struct share_flags<share::Anonymous> : std::integral_constant<int, MAP_PRIVATE | MAP_ANONYMOUS> {};
template <>
struct share_flags<share::Locked> : std::integral_constant<int, MAP_LOCKED> {};
template <>
struct share_flags<share::Populate> : std::integral_constant<int, MAP_POPULATE> {};
template <>
struct share_flags<share::HugeTLB> : std::integral_constant<int, MAP_HUGETLB | MAP_HUGE_2MB> {};

template <typename Share>
inline constexpr int share_flags_v = share_flags<Share>::value;

template <typename Share>
inline constexpr bool is_primary_share_v = std::is_same_v<Share, share::Private> || std::is_same_v<Share, share::Shared>
                                        || std::is_same_v<Share, share::Anonymous>;

// Both bit maps have a primary that answers zero, and zero is a value
// the kernel accepts: PROT_NONE maps a page nobody may touch, and a
// share word with no primary bit set is whatever the other flags say.
// A tag the map has never heard of would therefore fold silently into a
// mapping that is wrong rather than refused.
//
// These two are what the gates read instead of the bits.  They are hand
// lists, and a hand list cannot see what it omits, so the walk at the
// foot of this header reads fixy::mmap::prot and fixy::mmap::share and
// fails if a tag declared there is missing from either.
template <typename Prot>
inline constexpr bool is_known_prot_v = std::is_same_v<Prot, prot::ReadOnly> || std::is_same_v<Prot, prot::WriteCopy>
                                     || std::is_same_v<Prot, prot::ReadWrite> || std::is_same_v<Prot, prot::Exec>;

template <typename Share>
inline constexpr bool is_known_share_v = is_primary_share_v<Share> || std::is_same_v<Share, share::Locked>
                                      || std::is_same_v<Share, share::Populate>
                                      || std::is_same_v<Share, share::HugeTLB>;

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

template <typename A>
inline constexpr bool is_trusted_jit_v = std::is_same_v<std::remove_cvref_t<A>, atom_mmap::trusted_jit>;

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
inline constexpr bool has_trusted_jit_v = (is_trusted_jit_v<Atoms> || ...);

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

// An atom of another kind contributes nothing, so the fold is an OR over
// the whole pack and needs no filtering.
template <typename A>
inline constexpr int atom_prot_bits_v = is_with_prot_v<A> ? prot_bits_v<extract_prot_t<std::remove_cvref_t<A>>> : 0;

template <typename A>
inline constexpr int atom_share_flags_v =
    is_with_share_v<A> ? share_flags_v<extract_share_t<std::remove_cvref_t<A>>> : 0;

template <typename... Atoms>
[[nodiscard]] consteval int fold_prot_bits() noexcept {
    int acc = 0;
    ((acc |= atom_prot_bits_v<Atoms>), ...);
    return acc;
}

template <typename... Atoms>
[[nodiscard]] consteval int fold_share_flags() noexcept {
    int acc = 0;
    ((acc |= atom_share_flags_v<Atoms>), ...);
    return acc;
}

template <typename A>
inline constexpr bool is_exec_prot_v = std::is_same_v<std::remove_cvref_t<A>, atom_mmap::with_prot<prot::Exec>>;

template <typename... Atoms>
inline constexpr bool has_exec_prot_v = (is_exec_prot_v<Atoms> || ...);

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
                        && (::foundation::effects::LiftsToRow<std::remove_cvref_t<Atoms>> && ...)
                        && ::foundation::effects::CtxAdmits<Ctx, detail::atoms_row_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsMmapMint = CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
                       && detail::has_prot_atom_v<Atoms...> && detail::has_primary_share_atom_v<Atoms...>
                       && !detail::has_duplicate_prot_v<Atoms...> && !detail::has_duplicate_primary_share_v<Atoms...>
                       && (!detail::has_exec_prot_v<Atoms...> || detail::has_trusted_jit_v<Atoms...>);

template <typename Ctx, typename... Atoms>
concept CtxFitsAnonMmapMint = CtxFitsMmapMint<Ctx, Atoms...> && detail::pack_has_anonymous_v<Atoms...>;

// The advice tags lift nothing, so these two gates name the row the
// madvise call itself needs rather than deriving it.  Both calls reach
// the same syscall as the mints above and can park for the same
// reasons.
template <typename Ctx>
concept CtxAdmitsAdvise =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

template <typename Ctx, typename Advice>
concept CtxFitsSafeAdvise = CtxAdmitsAdvise<Ctx> && (advice_value_v<Advice> >= 0) && !is_dangerous_advice_v<Advice>;

template <typename Ctx, typename Advice>
concept CtxFitsReleaseAwareAdvise =
    CtxAdmitsAdvise<Ctx> && (advice_value_v<Advice> >= 0) && is_dangerous_advice_v<Advice>;

// The descriptor is a plain int rather than an owning handle type, so
// that a descriptor from any source reaches this without a conversion.
//
// §XXI carve-out: cx=alloc — mapping is a kernel side effect.
template <typename Tag, typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsMmapMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>>>, std::error_code>
mint_mmap(Ctx const&, int fd, std::size_t length, ::off_t offset = 0) noexcept {
    constexpr int prot = detail::fold_prot_bits<Atoms...>();
    constexpr int flags = detail::fold_share_flags<Atoms...>();
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>>;
    // The syscall lives with the region's only constructor, in
    // fixy/OwnedMmap.h, so that no address but the kernel's can become a
    // region.  What this mint owns is the gate above: which atoms, and
    // which context.
    auto region = Region::map_region(prot, flags, fd, length, offset);
    if (!region) {
        return std::unexpected{std::error_code{region.error(), std::system_category()}};
    }
    return mint_linear<Region>(std::move(*region));
}

// An anonymous mapping takes a descriptor of -1 and an offset of 0,
// which is the convention mmap(2) states.
//
// §XXI carve-out: cx=alloc — mapping is a kernel side effect.
template <typename Tag, typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsAnonMmapMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<
    Linear<OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>>>, std::error_code>
mint_mmap_anon(Ctx const&, std::size_t length) noexcept {
    constexpr int prot = detail::fold_prot_bits<Atoms...>();
    constexpr int flags = detail::fold_share_flags<Atoms...>();
    using Region = OwnedMmap<Tag, detail::prot_of_t<Atoms...>, detail::primary_share_of_t<Atoms...>>;
    auto region = Region::map_region(prot, flags, -1, length, 0);
    if (!region) {
        return std::unexpected{std::error_code{region.error(), std::system_category()}};
    }
    return mint_linear<Region>(std::move(*region));
}

template <typename Advice, typename Tag, typename Prot, typename Share, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSafeAdvise<Ctx, Advice>
[[nodiscard]] inline std::expected<void, std::error_code> advise(Ctx const&,
                                                                 OwnedMmap<Tag, Prot, Share>& region) noexcept {
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
// arriving here, and the tag on it must match the region, so a
// permission over some other region cannot stand in.
//
// It is borrowed rather than consumed because discarding pages leaves
// the region usable, so the caller keeps it and may hand it back out
// afterwards.
template <typename Advice, typename RegionTag, typename Tag, typename Prot, typename Share,
          ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsReleaseAwareAdvise<Ctx, Advice>
[[nodiscard]] inline std::expected<void, std::error_code>
advise_release_aware(Ctx const&, OwnedMmap<Tag, Prot, Share>& region,
                     ::foundation::permissions::Permission<RegionTag> const& /*exclusive_proof*/) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>)
        < 0) {  // SYSCALL-CAP-OK: advise_release_aware ctx-gate (CtxFitsReleaseAwareAdvise: IO+Block + Permission<RegionTag>)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

}  // namespace fixy::mmap

namespace fixy::mmap::detail::mmap_surface_invariants {

static_assert(prot_bits_v<prot::ReadOnly> == PROT_READ);
static_assert(prot_bits_v<prot::WriteCopy> == (PROT_READ | PROT_WRITE));
static_assert(prot_bits_v<prot::ReadWrite> == (PROT_READ | PROT_WRITE));
static_assert(prot_bits_v<prot::Exec> == (PROT_READ | PROT_EXEC));
static_assert((prot_bits_v<prot::Exec> & PROT_WRITE) == 0, "W^X: prot::Exec must NOT include PROT_WRITE");

static_assert(share_flags_v<share::Private> == MAP_PRIVATE);
static_assert(share_flags_v<share::Shared> == MAP_SHARED);
static_assert((share_flags_v<share::Anonymous> & MAP_ANONYMOUS) != 0);

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
static_assert(has_exec_prot_v<A_Exec, A_Shared>);
static_assert(!has_exec_prot_v<A_RO, A_Shared>);
static_assert(has_trusted_jit_v<A_Jit, A_Shared>);
static_assert(!has_trusted_jit_v<A_RO, A_Shared>);
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

static_assert(fold_prot_bits<A_RO, A_Exec, A_Jit>() == (PROT_READ | PROT_EXEC));
static_assert(fold_share_flags<A_Shared, A_Locked>() == (MAP_SHARED | MAP_LOCKED));

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
static_assert(CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::DontNeed>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, advice::HugePage>,
              "the release-aware door is for the dangerous advice only; the rest go through advise.");

// The bit maps answer zero for a tag they do not know, and the gate
// reads the known-tag predicates instead.  These cells are the witness
// that it does: an atom over a tag with no mapping is refused rather
// than folded into PROT_NONE.
struct NotAProt final {};
struct NotAShare final {};
using A_UnknownProt = ::fixy::atom::mmap::with_prot<NotAProt>;
using A_UnknownShare = ::fixy::atom::mmap::with_share<NotAShare>;

static_assert(prot_bits_v<NotAProt> == 0, "an unmapped prot tag folds to PROT_NONE, which the kernel accepts.");
static_assert(share_flags_v<NotAShare> == 0);
static_assert(!is_known_prot_v<NotAProt>);
static_assert(!is_known_share_v<NotAShare>);
static_assert(all_atom_tags_known_v<A_RO, A_Shared>);
static_assert(!all_atom_tags_known_v<A_UnknownProt, A_Shared>);
static_assert(!all_atom_tags_known_v<A_RO, A_Shared, A_UnknownShare>);
static_assert(!CtxFitsMmapMint<IoBlockCtx, A_UnknownProt, A_Shared>,
              "a prot tag with no PROT_* mapping must be refused at the gate, not mapped as PROT_NONE.");

// Every tag fixy::mmap::prot and fixy::mmap::share declare is known to
// the bit map above.  The predicates are hand lists, and this is what
// notices a tag the lists omit.
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
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::prot is missing from is_known_prot_v, so the gate "
              "would admit it and the fold would map it as PROT_NONE.");
static_assert(every_tag_in_is_known_<^^::fixy::mmap::share, false>(),
              "fixy/os/Mmap.h: a tag declared in fixy::mmap::share is missing from is_known_share_v, so the gate "
              "would admit it and the fold would contribute no MAP_* bit for it.");

// A type that is not an advice tag has no value, and the gate reads
// that rather than instantiating madvise with -1.
struct NotAnAdvice final {};
static_assert(advice_value_v<NotAnAdvice> == -1);
static_assert(!CtxFitsSafeAdvise<IoBlockCtx, NotAnAdvice>);
static_assert(!CtxFitsReleaseAwareAdvise<IoBlockCtx, NotAnAdvice>);

// Every class declared in fixy::mmap::advice is an empty final type
// that is not an atom: the shape of a tag.  This is the walk Os.h runs
// over its nine namespaces, run here over the tenth, because the advice
// tags live beside the function that reads them rather than beside the
// atoms.  When Os.h next changes, ^^::fixy::mmap::advice belongs in its
// os_tag_namespaces and this walk can go.
[[nodiscard]] consteval bool every_advice_class_is_tag_() noexcept {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^::fixy::mmap::advice, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if constexpr (!std::is_empty_v<T> || !std::is_final_v<T> || ::fixy::atom::IsAtom<T>) return false;
            if (advice_value_v<T> < 0) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval std::size_t advice_tag_count_() noexcept {
    std::size_t count = 0;
    for (const auto member :
         std::meta::members_of(^^::fixy::mmap::advice, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(member) || std::meta::is_type_alias(member) || !std::meta::is_class_type(member))
            continue;
        ++count;
    }
    return count;
}

static_assert(every_advice_class_is_tag_(),
              "fixy/os/Mmap.h: a class declared in fixy::mmap::advice has state, is not final, is an atom, or "
              "has no MADV_* value.  A tag is an empty final type with a value and nothing else.");

// The walk sees whatever is declared, so it cannot notice a tag that was
// deleted.  This count is what does.
static_assert(advice_tag_count_() == 10, "fixy/os/Mmap.h: fixy::mmap::advice holds a different number of tags than "
                                         "this pin records.  A new advice raises it; one that disappeared is a "
                                         "deletion somebody has to justify.");

}  // namespace fixy::mmap::detail::mmap_surface_invariants
