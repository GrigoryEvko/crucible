#pragma once

#include <crucible/fixy/Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/OwnedMmap.h>
#include <crucible/permissions/Permission.h>

#include <crucible/effects/_ExecCtx.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_Capabilities.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
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

namespace crucible::fixy::mmap {

// Write and execute are never both set: Exec carries read and execute only.
// A JIT that has to stage writes maps the same pages twice, once writable for
// code generation and once executable to run them, which is the discipline
// hardware execute-only memory assumes anyway.
//
// WriteCopy and ReadWrite carry identical bits and differ only in the share
// mode they are meant to accompany.

namespace prot {
struct ReadOnly final {};
struct WriteCopy final {};  // copy-on-write; goes with share::Private
struct ReadWrite final {};  // goes with share::Shared
struct Exec final {};  // reachable only with the trusted_jit grant
}  // namespace prot

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

// The first three are primary modes and a mapping has exactly one.  The last
// three are flags that stack on any primary.

namespace share {
struct Private final {};
struct Shared final {};
struct Anonymous final {};  // zero-filled, no file behind it
struct Locked final {};  // keeps pages off swap, against RLIMIT_MEMLOCK
struct Populate final {};  // prefaults every page
struct HugeTLB final {};  // 2 MiB pages
}  // namespace share

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

// DontNeed is the one that has to be handled apart from the rest: it zeros
// the pages, which races any concurrent reader of the region.  The plain
// advise surface refuses it and the release-aware one takes a witness.

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

// A named predicate rather than an inline comparison, because the set can
// grow: a kernel that starts discarding eagerly where it discards lazily
// today moves that advice into this set.

template <typename Advice>
inline constexpr bool is_dangerous_advice_v = std::is_same_v<Advice, advice::DontNeed>;

}  // namespace crucible::fixy::mmap

// An explicit specialization has to appear inside the namespace of the
// template it specializes, so the grant namespace is reopened here rather
// than the tags living beside their axes.

namespace crucible::fixy::grant {

namespace mmap {

template <typename Prot>
struct with_prot final : grant_base {};

template <typename Share>
struct with_share final : grant_base {};

template <typename Advice>
struct with_advice final : grant_base {};

// This grant is what admits an executable mapping.  It asserts that the
// caller has audited the bytes that will run and holds to the discipline
// that keeps writing and executing in separate mappings.

struct trusted_jit final : grant_base {};

// The tag names which region's readers the caller is claiming to have
// accounted for.  Naming it is what stops a permission over one region from
// standing in for another.

template <typename RegionTag>
struct release_aware final : grant_base {};

}  // namespace mmap

// This grant is what lets a caller give up a mapped region without
// unmapping it.  Its tag names why that is acceptable — the region was
// handed to a kernel ring buffer, to a socket memory pool, and so on.  Each
// call site declares its own tag, so the reason is in the source the
// reviewer reads and every such site is findable by its tag.

namespace leak {

template <typename RationaleTag>
struct resource final : grant_base {};

}  // namespace leak

template <typename Prot>
struct which_dim<mmap::with_prot<Prot>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Share>
struct which_dim<mmap::with_share<Share>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Advice>
struct which_dim<mmap::with_advice<Advice>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<mmap::trusted_jit> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename RegionTag>
struct which_dim<mmap::release_aware<RegionTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

// A deliberate leak is the absence of the matching unmap call, and the axis
// tracks which syscall surface a site engages, so its absence belongs on the
// same axis.

template <typename RationaleTag>
struct which_dim<leak::resource<RationaleTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

// The trait's primary template is declared where the region type lives and
// never names this grant family.  Only this specialization makes the family
// satisfy it, which is how the lower layer stays unaware of the grant.

namespace crucible::safety {

template <typename RationaleTag>
struct is_leak_grant<::crucible::fixy::grant::leak::resource<RationaleTag>> : std::true_type {};

}  // namespace crucible::safety

namespace crucible::fixy::mmap {

namespace detail {

template <typename G>
struct extract_prot {
    using type = void;
};
template <typename Prot>
struct extract_prot<::crucible::fixy::grant::mmap::with_prot<Prot>> {
    using type = Prot;
};
template <typename G>
using extract_prot_t = typename extract_prot<G>::type;

template <typename G>
struct is_with_prot : std::false_type {};
template <typename Prot>
struct is_with_prot<::crucible::fixy::grant::mmap::with_prot<Prot>> : std::true_type {};
template <typename G>
inline constexpr bool is_with_prot_v = is_with_prot<G>::value;

template <typename G>
struct is_with_share : std::false_type {};
template <typename Share>
struct is_with_share<::crucible::fixy::grant::mmap::with_share<Share>> : std::true_type {};
template <typename G>
inline constexpr bool is_with_share_v = is_with_share<G>::value;

template <typename G>
struct is_primary_with_share : std::false_type {};
template <typename Share>
struct is_primary_with_share<::crucible::fixy::grant::mmap::with_share<Share>>
    : std::bool_constant<is_primary_share_v<Share>> {};
template <typename G>
inline constexpr bool is_primary_with_share_v = is_primary_with_share<G>::value;

template <typename G>
struct is_with_advice : std::false_type {};
template <typename Advice>
struct is_with_advice<::crucible::fixy::grant::mmap::with_advice<Advice>> : std::true_type {};
template <typename G>
inline constexpr bool is_with_advice_v = is_with_advice<G>::value;

template <typename G>
struct is_trusted_jit : std::false_type {};
template <>
struct is_trusted_jit<::crucible::fixy::grant::mmap::trusted_jit> : std::true_type {};
template <typename G>
inline constexpr bool is_trusted_jit_v = is_trusted_jit<G>::value;

template <typename G>
struct is_release_aware : std::false_type {};
template <typename RegionTag>
struct is_release_aware<::crucible::fixy::grant::mmap::release_aware<RegionTag>> : std::true_type {};
template <typename G>
inline constexpr bool is_release_aware_v = is_release_aware<G>::value;

template <typename... Grants>
inline constexpr bool has_prot_grant_v = (is_with_prot_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_primary_share_grant_v = (is_primary_with_share_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_trusted_jit_v = (is_trusted_jit_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_release_aware_v = (is_release_aware_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_duplicate_prot_v = (static_cast<int>(is_with_prot_v<Grants>) + ...) > 1;

template <typename... Grants>
inline constexpr bool has_duplicate_primary_share_v = (static_cast<int>(is_primary_with_share_v<Grants>) + ...) > 1;

template <typename... Grants>
struct prot_of;
template <typename First, typename... Rest>
struct prot_of<First, Rest...> {
    using type = std::conditional_t<is_with_prot_v<First>, extract_prot_t<First>, typename prot_of<Rest...>::type>;
};
template <>
struct prot_of<> {
    using type = void;
};
template <typename... Grants>
using prot_of_t = typename prot_of<Grants...>::type;

template <typename G>
struct extract_share {
    using type = void;
};
template <typename Share>
struct extract_share<::crucible::fixy::grant::mmap::with_share<Share>> {
    using type = Share;
};
template <typename G>
using extract_share_t = typename extract_share<G>::type;

template <typename... Grants>
struct primary_share_of;
template <typename First, typename... Rest>
struct primary_share_of<First, Rest...> {
    using type = std::conditional_t<is_primary_with_share_v<First>, extract_share_t<First>,
                                    typename primary_share_of<Rest...>::type>;
};
template <>
struct primary_share_of<> {
    using type = void;
};
template <typename... Grants>
using primary_share_of_t = typename primary_share_of<Grants...>::type;

template <typename G>
struct grant_prot_bits : std::integral_constant<int, 0> {};
template <typename Prot>
struct grant_prot_bits<::crucible::fixy::grant::mmap::with_prot<Prot>>
    : std::integral_constant<int, prot_bits_v<Prot>> {};
template <typename G>
inline constexpr int grant_prot_bits_v = grant_prot_bits<G>::value;

template <typename... Grants>
inline constexpr int fold_prot_bits() noexcept {
    int acc = 0;
    ((acc |= grant_prot_bits_v<Grants>), ...);
    return acc;
}

template <typename G>
struct grant_share_flags : std::integral_constant<int, 0> {};
template <typename Share>
struct grant_share_flags<::crucible::fixy::grant::mmap::with_share<Share>>
    : std::integral_constant<int, share_flags_v<Share>> {};
template <typename G>
inline constexpr int grant_share_flags_v = grant_share_flags<G>::value;

template <typename... Grants>
inline constexpr int fold_share_flags() noexcept {
    int acc = 0;
    ((acc |= grant_share_flags_v<Grants>), ...);
    return acc;
}

template <typename G>
struct is_exec_prot : std::false_type {};
template <>
struct is_exec_prot<::crucible::fixy::grant::mmap::with_prot<prot::Exec>> : std::true_type {};
template <typename G>
inline constexpr bool is_exec_prot_v = is_exec_prot<G>::value;

template <typename... Grants>
inline constexpr bool has_exec_prot_v = (is_exec_prot_v<Grants> || ...);

template <typename G>
struct is_anonymous_share : std::false_type {};
template <>
struct is_anonymous_share<::crucible::fixy::grant::mmap::with_share<share::Anonymous>> : std::true_type {};
template <typename G>
inline constexpr bool is_anonymous_share_v = is_anonymous_share<G>::value;

template <typename... Grants>
inline constexpr bool pack_has_anonymous_v = (is_anonymous_share_v<Grants> || ...);

}  // namespace detail

// The region type itself lives a layer down, so that a consumer which needs
// the unmapping discipline but none of this syscall surface can reach it
// without this header.  The two spellings name one type.

template <typename Tag, typename Prot, typename Share>
using OwnedMmap = ::crucible::safety::OwnedMmap<Tag, Prot, Share>;

// Mapping and unmapping can both park the caller — on page-cache pressure,
// on a NUMA-remote page fault, on write-back — so Block is required
// alongside IO, as for any other filesystem-touching call.

template <typename Ctx>
concept CtxAdmitsIoBlock =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::IO>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::Block>;

template <typename Ctx, typename... Grants>
concept CtxFitsMmapMint =
    CtxAdmitsIoBlock<Ctx> && detail::has_prot_grant_v<Grants...> && detail::has_primary_share_grant_v<Grants...>
    && !detail::has_duplicate_prot_v<Grants...> && !detail::has_duplicate_primary_share_v<Grants...>
    && (!detail::has_exec_prot_v<Grants...> || detail::has_trusted_jit_v<Grants...>);

template <typename Ctx, typename... Grants>
concept CtxFitsAnonMmapMint = CtxFitsMmapMint<Ctx, Grants...> && detail::pack_has_anonymous_v<Grants...>;

template <typename Ctx, typename Advice>
concept CtxFitsSafeAdvise = CtxAdmitsIoBlock<Ctx> && !is_dangerous_advice_v<Advice>;

template <typename Ctx, typename Advice, typename RegionTag>
concept CtxFitsReleaseAwareAdvise = CtxAdmitsIoBlock<Ctx> && is_dangerous_advice_v<Advice>;

// The descriptor is a plain int rather than an owning handle type, so that a
// descriptor from any source reaches this without a conversion.

template <typename Tag, typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMmapMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<OwnedMmap<Tag, detail::prot_of_t<Grants...>, detail::primary_share_of_t<Grants...>>>,
    std::error_code>
mint_mmap(Ctx const&, int fd, std::size_t length, ::off_t offset = 0) noexcept {
    constexpr int prot_bits = detail::fold_prot_bits<Grants...>();
    constexpr int share_flags = detail::fold_share_flags<Grants...>();
    void* const addr =
        ::mmap(nullptr, length, prot_bits, share_flags, fd,
               offset);  // SYSCALL-CAP-OK: mint_mmap ctx-gate (CtxFitsMmapMint: CtxAdmitsIoBlock, effects::IO+Block)
    if (addr == MAP_FAILED) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    using Region = OwnedMmap<Tag, detail::prot_of_t<Grants...>, detail::primary_share_of_t<Grants...>>;
    return ::crucible::safety::Linear<Region>{Region{addr, length}};
}

// An anonymous mapping takes a descriptor of -1 and an offset of 0, which is
// the convention mmap(2) states.

template <typename Tag, typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAnonMmapMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<OwnedMmap<Tag, detail::prot_of_t<Grants...>, detail::primary_share_of_t<Grants...>>>,
    std::error_code>
mint_mmap_anon(Ctx const&, std::size_t length) noexcept {
    constexpr int prot_bits = detail::fold_prot_bits<Grants...>();
    constexpr int share_flags = detail::fold_share_flags<Grants...>();
    void* const addr = ::mmap(
        nullptr, length, prot_bits, share_flags, -1,
        0);  // SYSCALL-CAP-OK: mint_mmap_anon ctx-gate (CtxFitsAnonMmapMint: CtxAdmitsIoBlock, effects::IO+Block)
    if (addr == MAP_FAILED) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    using Region = OwnedMmap<Tag, detail::prot_of_t<Grants...>, detail::primary_share_of_t<Grants...>>;
    return ::crucible::safety::Linear<Region>{Region{addr, length}};
}

template <typename Advice, typename Tag, typename Prot, typename Share, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSafeAdvise<Ctx, Advice>
[[nodiscard]] inline std::expected<void, std::error_code> advise(Ctx const&,
                                                                 OwnedMmap<Tag, Prot, Share>& region) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>)
        < 0) {  // SYSCALL-CAP-OK: advise ctx-gate (CtxFitsSafeAdvise: CtxAdmitsIoBlock, effects::IO+Block)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// What makes this surface safe is the permission the caller has to produce.
// An exclusive permission over a region is obtainable only once every
// outstanding share of it has been deposited back, so holding one witnesses
// that no reader is live.  The permission is move-only, so the caller cannot
// have handed it to a reader between obtaining it and arriving here, and the
// tag on it must match the region, so a permission over some other region
// cannot stand in.
//
// It is borrowed rather than consumed because discarding pages leaves the
// region usable, so the caller keeps it and may hand it back out afterwards.

template <typename Advice, typename RegionTag, typename Tag, typename Prot, typename Share,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsReleaseAwareAdvise<Ctx, Advice, RegionTag>
[[nodiscard]] inline std::expected<void, std::error_code>
advise_release_aware(Ctx const&, OwnedMmap<Tag, Prot, Share>& region,
                     ::crucible::safety::Permission<RegionTag> const& /*exclusive_proof*/) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>)
        < 0) {  // SYSCALL-CAP-OK: advise_release_aware ctx-gate (CtxFitsReleaseAwareAdvise: IO+Block + Permission<RegionTag>)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

namespace self_test {

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

using G_RO_Priv = ::crucible::fixy::grant::mmap::with_prot<prot::ReadOnly>;
using G_RW_Shar = ::crucible::fixy::grant::mmap::with_share<share::Shared>;
using G_RW_Priv = ::crucible::fixy::grant::mmap::with_share<share::Private>;
using G_Anon = ::crucible::fixy::grant::mmap::with_share<share::Anonymous>;
using G_Locked = ::crucible::fixy::grant::mmap::with_share<share::Locked>;
using G_Exec = ::crucible::fixy::grant::mmap::with_prot<prot::Exec>;
using G_Jit = ::crucible::fixy::grant::mmap::trusted_jit;

static_assert(detail::has_prot_grant_v<G_RO_Priv, G_RW_Shar>);
static_assert(!detail::has_prot_grant_v<G_RW_Shar>);
static_assert(detail::has_primary_share_grant_v<G_RO_Priv, G_RW_Shar>);
static_assert(detail::has_primary_share_grant_v<G_RO_Priv, G_Anon>);
static_assert(!detail::has_primary_share_grant_v<G_RO_Priv, G_Locked>);
static_assert(detail::has_duplicate_prot_v<G_RO_Priv, G_RO_Priv>);
static_assert(!detail::has_duplicate_prot_v<G_RO_Priv, G_RW_Shar>);
static_assert(detail::has_duplicate_primary_share_v<G_RW_Shar, G_RW_Priv>);
static_assert(!detail::has_duplicate_primary_share_v<G_RW_Shar, G_Locked>);
static_assert(detail::has_exec_prot_v<G_Exec, G_RW_Shar>);
static_assert(!detail::has_exec_prot_v<G_RO_Priv, G_RW_Shar>);
static_assert(detail::has_trusted_jit_v<G_Jit, G_RW_Shar>);
static_assert(!detail::has_trusted_jit_v<G_RO_Priv, G_RW_Shar>);
static_assert(detail::pack_has_anonymous_v<G_RO_Priv, G_Anon>);
static_assert(!detail::pack_has_anonymous_v<G_RO_Priv, G_RW_Shar>);

static_assert(std::is_same_v<detail::prot_of_t<G_RO_Priv, G_RW_Shar>, prot::ReadOnly>);
static_assert(std::is_same_v<detail::prot_of_t<G_RW_Shar, G_Exec, G_Jit>, prot::Exec>);
static_assert(std::is_same_v<detail::primary_share_of_t<G_RO_Priv, G_RW_Shar>, share::Shared>);
static_assert(std::is_same_v<detail::primary_share_of_t<G_RO_Priv, G_Anon, G_Locked>, share::Anonymous>);

static_assert(detail::fold_prot_bits<G_RO_Priv, G_Exec, G_Jit>() == (PROT_READ | PROT_EXEC));
static_assert(detail::fold_share_flags<G_RW_Shar, G_Locked>() == (MAP_SHARED | MAP_LOCKED));

struct RegionA {};
using TestRegion = OwnedMmap<RegionA, prot::ReadOnly, share::Private>;

static_assert(!std::is_copy_constructible_v<TestRegion>, "OwnedMmap must be move-only — copy would double-unmap");
static_assert(!std::is_copy_assignable_v<TestRegion>);
static_assert(std::is_nothrow_move_constructible_v<TestRegion>);
static_assert(std::is_nothrow_move_assignable_v<TestRegion>);
static_assert(std::is_nothrow_default_constructible_v<TestRegion>);
static_assert(sizeof(TestRegion) == sizeof(void*) + sizeof(std::size_t),
              "OwnedMmap is exactly {addr, len} — no hidden padding");

// The context named below carries an effect row with both IO and Block, so
// it is the one that reaches every gate here.

static_assert(CtxFitsMmapMint<::crucible::effects::TestRunnerCtx, G_RO_Priv, G_RW_Shar>);

static_assert(CtxFitsAnonMmapMint<::crucible::effects::TestRunnerCtx, G_RO_Priv, G_Anon>);

static_assert(!CtxFitsAnonMmapMint<::crucible::effects::TestRunnerCtx, G_RO_Priv, G_RW_Shar>);

static_assert(!CtxFitsMmapMint<::crucible::effects::TestRunnerCtx, G_Exec, G_RW_Priv>);
static_assert(CtxFitsMmapMint<::crucible::effects::TestRunnerCtx, G_Exec, G_RW_Priv, G_Jit>);

static_assert(!CtxFitsMmapMint<::crucible::effects::TestRunnerCtx>);

static_assert(CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::HugePage>);
static_assert(CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::Sequential>);
static_assert(!CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::DontNeed>);
static_assert(CtxFitsReleaseAwareAdvise<::crucible::effects::TestRunnerCtx, advice::DontNeed, RegionA>);
static_assert(!CtxFitsReleaseAwareAdvise<::crucible::effects::TestRunnerCtx, advice::HugePage, RegionA>);

struct DummyLeakRationale {};
using LeakGrant = ::crucible::fixy::grant::leak::resource<DummyLeakRationale>;

static_assert(::crucible::safety::is_leak_grant_v<LeakGrant>, "leak::resource<RationaleTag> must satisfy IsLeakGrant");
static_assert(::crucible::safety::IsLeakGrant<LeakGrant>,
              "leak::resource<RationaleTag> must satisfy IsLeakGrant concept");
static_assert(!::crucible::safety::is_leak_grant_v<int>, "int must NOT satisfy IsLeakGrant");
static_assert(!::crucible::safety::is_leak_grant_v<G_RO_Priv>, "non-leak grants must NOT satisfy IsLeakGrant");

}  // namespace self_test

}  // namespace crucible::fixy::mmap
