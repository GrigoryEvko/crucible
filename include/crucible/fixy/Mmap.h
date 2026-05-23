#pragma once

// ── crucible::fixy::mmap — typed memory-mapping surface (FIXY-V-225) ──
//
// Three orthogonal axis namespaces under `crucible::fixy::mmap`:
//
//   prot::{ReadOnly, WriteCopy, ReadWrite, Exec}
//                                         — protection bit tier
//   share::{Private, Shared, Anonymous, Locked, Populate, HugeTLB}
//                                         — primary mode + additive flags
//   advice::{HugePage, NoHugePage, Collapse, Sequential, Random,
//            WillNeed, DontNeed, Free, WipeOnFork, DontDump}
//                                         — madvise(2) hint tier
//
// Grant tags (each `final : grant::grant_base`) route through
// `DimensionAxis::SyscallSurface` (V-097's enumerator; the mmap/munmap/
// madvise trio IS a syscall surface tier):
//
//   crucible::fixy::grant::mmap::with_prot<Prot>
//   crucible::fixy::grant::mmap::with_share<Share>
//   crucible::fixy::grant::mmap::with_advice<Advice>
//   crucible::fixy::grant::mmap::trusted_jit           — Exec gating
//   crucible::fixy::grant::mmap::release_aware<RegionTag>
//                                         — Bug 5 typed-witness (V-234
//                                           composes with SharedPermission)
//
// One Linear move-only RAII type:
//
//   OwnedMmap<Tag, Prot, Share>           — destructor calls ::munmap;
//                                            data()/size()/is_mapped()
//                                            accessors; non-copyable.
//
// Three call-site factories per CLAUDE.md §XXI:
//
//   mint_mmap<Tag, Grants...>(ctx, fh, length, offset)
//         — §XXI mint; file-backed mapping over an open FileHandle.
//           Requires CtxFitsMmapMint<Ctx, Grants...> (single concept).
//
//   mint_mmap_anon<Tag, Grants...>(ctx, length)
//         — §XXI mint; anonymous mapping (no file backing).
//           Requires CtxFitsAnonMmapMint<Ctx, Grants...> which adds the
//           "Grants engages with_share<Anonymous>" predicate.
//
// Two effect operations (NOT §XXI mints — they perform syscalls on an
// existing region, no fresh authoritative resource synthesized):
//
//   advise<Advice>(ctx, OwnedMmap&)
//         — safe-surface madvise; accepts every advice EXCEPT DontNeed.
//           DontNeed is dangerous (zeros pages out from under any
//           concurrent ReadView).  Caller must use advise_release_aware
//           for that one.
//
//   advise_release_aware<Advice, RegionTag>(ctx, OwnedMmap&)
//         — Bug 5 narrow surface for DontNeed (and future MADV_FREE
//           variants that zero pages).  Compile-time witness of the
//           release_aware<RegionTag> grant family.  V-234 will extend
//           this with a SharedPermission proof obligation that no
//           live ReadView<RegionTag> exists at the call site.
//
// ── Axiom coverage (code_guide §II) ───────────────────────────────────
//
//   InitSafe   — every tag is `final` empty struct, NSDMI-trivial;
//                OwnedMmap default-ctor leaves MAP_FAILED sentinel;
//                std::expected return channel.
//   TypeSafe   — strong types for every axis value (no raw int PROT_*/
//                MAP_*/MADV_* in the public surface).
//   NullSafe   — std::expected, no raw pointer return; is_mapped()
//                gate before every dereference.
//   MemSafe    — OwnedMmap is move-only; dtor unmaps; Linear gate at
//                mint boundary forces explicit consume.
//   BorrowSafe — mint_mmap consumes Grants pack by template; no shared
//                mutable state in the factories.
//   ThreadSafe — every factory is pure / stateless.  munmap is the
//                only syscall that mutates process VA on destruction;
//                no cross-thread races within a single OwnedMmap.
//   LeakSafe   — OwnedMmap dtor unconditionally unmaps on is_mapped();
//                move-assignment unmaps the target before overwrite.
//   DetSafe    — same length + same Grants + same ctx → same mmap()
//                syscall sequence; mmap address itself depends on
//                kernel ASLR (intentionally non-deterministic for
//                security — this is fine for DetSafe because no
//                replay path observes the address).
//
// ── HS14 fixtures (≥6 per §XXI / CLAUDE.md HS14) ──────────────────────
//
// Each mismatch class lives in test/fixy_neg/neg_fixy_v_225_*.cpp:
//
//   1. mint_mmap<...>(ctx) with empty Grants — no prot, no share.
//   2. mint_mmap with with_prot<Exec> but no trusted_jit grant.
//   3. mint_mmap with two with_prot<X> grants — duplicate-prot reject.
//   4. mint_mmap with two primary-share grants — duplicate reject.
//   5. mint_mmap_anon without with_share<Anonymous> — gate fires.
//   6. mint_mmap in a ColdInitCtx (Row<Init,Alloc,IO>, no Block).
//   7. advise<DontNeed> on the safe surface — must use advise_release_aware.

#include <crucible/fixy/Grant.h>            // grant_base, which_dim primary
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/safety/Linear.h>         // safety::Linear

#include <crucible/effects/ExecCtx.h>       // IsExecCtx + row_type_of_t
#include <crucible/effects/EffectRow.h>     // row_contains_v
#include <crucible/effects/Capabilities.h>  // effects::Effect

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

// ── Linux defensive constants — only define if absent ────────────────
//
// Crucible targets Linux exclusively (CLAUDE.md §XIV).  Defensive
// macros guard the few flags that landed in newer kernels so this
// header builds against any libc that doesn't yet expose them.

#ifndef MADV_COLLAPSE
#  define MADV_COLLAPSE 25   // Linux 6.1 (2022-12).
#endif
#ifndef MADV_FREE
#  define MADV_FREE 8        // Linux 4.5 (2016-03).
#endif
#ifndef MADV_WIPEONFORK
#  define MADV_WIPEONFORK 18 // Linux 4.14 (2017-11).
#endif
#ifndef MADV_DONTDUMP
#  define MADV_DONTDUMP 16   // Linux 3.4 (2012-05).
#endif
#ifndef MAP_HUGE_2MB
#  define MAP_HUGE_2MB (21 << 26)
#endif

namespace crucible::fixy::mmap {

// ═════════════════════════════════════════════════════════════════════
// ── (a) prot — protection bit tier ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Four tier markers.  W^X is structural: prot::Exec is PROT_READ|
// PROT_EXEC ONLY (never paired with PROT_WRITE).  A JIT that needs to
// stage writes must dual-map (one writable region for code generation,
// one executable region for execution) — this is the same discipline
// hardware-enforced memory protection (XOM, CET shadow stacks) assumes.

namespace prot {
struct ReadOnly  final {};   // PROT_READ
struct WriteCopy final {};   // PROT_READ | PROT_WRITE — COW, pairs with Private
struct ReadWrite final {};   // PROT_READ | PROT_WRITE — pairs with Shared
struct Exec      final {};   // PROT_READ | PROT_EXEC — gated by trusted_jit
}  // namespace prot

template <typename Prot>
struct prot_bits : std::integral_constant<int, 0> {};
template <> struct prot_bits<prot::ReadOnly>  : std::integral_constant<int, PROT_READ> {};
template <> struct prot_bits<prot::WriteCopy> : std::integral_constant<int, PROT_READ | PROT_WRITE> {};
template <> struct prot_bits<prot::ReadWrite> : std::integral_constant<int, PROT_READ | PROT_WRITE> {};
template <> struct prot_bits<prot::Exec>      : std::integral_constant<int, PROT_READ | PROT_EXEC> {};

template <typename Prot>
inline constexpr int prot_bits_v = prot_bits<Prot>::value;

// ═════════════════════════════════════════════════════════════════════
// ── (b) share — primary mode + additive flags ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Three primary modes (mutually exclusive — exactly one per mint):
//   Private    MAP_PRIVATE                — COW backing
//   Shared     MAP_SHARED                 — visible to other mappers
//   Anonymous  MAP_PRIVATE|MAP_ANONYMOUS  — zero-fill, no file
//
// Three additive flags (stackable on any primary):
//   Locked     MAP_LOCKED                 — prevent swap (RLIMIT_MEMLOCK)
//   Populate   MAP_POPULATE               — prefault all pages
//   HugeTLB    MAP_HUGETLB|MAP_HUGE_2MB   — 2 MiB pages

namespace share {
struct Private    final {};
struct Shared     final {};
struct Anonymous  final {};
struct Locked     final {};
struct Populate   final {};
struct HugeTLB    final {};
}  // namespace share

template <typename Share>
struct share_flags : std::integral_constant<int, 0> {};
template <> struct share_flags<share::Private>   : std::integral_constant<int, MAP_PRIVATE> {};
template <> struct share_flags<share::Shared>    : std::integral_constant<int, MAP_SHARED> {};
template <> struct share_flags<share::Anonymous> : std::integral_constant<int, MAP_PRIVATE | MAP_ANONYMOUS> {};
template <> struct share_flags<share::Locked>    : std::integral_constant<int, MAP_LOCKED> {};
template <> struct share_flags<share::Populate>  : std::integral_constant<int, MAP_POPULATE> {};
template <> struct share_flags<share::HugeTLB>   : std::integral_constant<int, MAP_HUGETLB | MAP_HUGE_2MB> {};

template <typename Share>
inline constexpr int share_flags_v = share_flags<Share>::value;

// `is_primary_share_v<X>` — true iff X is one of the mutually-exclusive
// primary tiers.  Used by has_primary_share_v / has_duplicate_primary_v.

template <typename Share>
inline constexpr bool is_primary_share_v =
    std::is_same_v<Share, share::Private>
    || std::is_same_v<Share, share::Shared>
    || std::is_same_v<Share, share::Anonymous>;

// ═════════════════════════════════════════════════════════════════════
// ── (c) advice — madvise(2) hint tier ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Ten markers covering the production-relevant subset of MADV_*.
// `DontNeed` is the LOAD-BEARING dangerous case: it zeros pages, racing
// with any concurrent ReadView reader.  The safe-surface `advise()`
// refuses it; callers route through `advise_release_aware<DontNeed,
// RegionTag>` which carries the typed witness.

namespace advice {
struct HugePage     final {};  // MADV_HUGEPAGE
struct NoHugePage   final {};  // MADV_NOHUGEPAGE
struct Collapse     final {};  // MADV_COLLAPSE (kernel 6.1+)
struct Sequential   final {};  // MADV_SEQUENTIAL
struct Random       final {};  // MADV_RANDOM
struct WillNeed     final {};  // MADV_WILLNEED
struct DontNeed     final {};  // MADV_DONTNEED — DANGEROUS, zeros pages
struct Free         final {};  // MADV_FREE
struct WipeOnFork   final {};  // MADV_WIPEONFORK
struct DontDump     final {};  // MADV_DONTDUMP
}  // namespace advice

template <typename Advice>
struct advice_value : std::integral_constant<int, -1> {};
template <> struct advice_value<advice::HugePage>   : std::integral_constant<int, MADV_HUGEPAGE> {};
template <> struct advice_value<advice::NoHugePage> : std::integral_constant<int, MADV_NOHUGEPAGE> {};
template <> struct advice_value<advice::Collapse>   : std::integral_constant<int, MADV_COLLAPSE> {};
template <> struct advice_value<advice::Sequential> : std::integral_constant<int, MADV_SEQUENTIAL> {};
template <> struct advice_value<advice::Random>     : std::integral_constant<int, MADV_RANDOM> {};
template <> struct advice_value<advice::WillNeed>   : std::integral_constant<int, MADV_WILLNEED> {};
template <> struct advice_value<advice::DontNeed>   : std::integral_constant<int, MADV_DONTNEED> {};
template <> struct advice_value<advice::Free>       : std::integral_constant<int, MADV_FREE> {};
template <> struct advice_value<advice::WipeOnFork> : std::integral_constant<int, MADV_WIPEONFORK> {};
template <> struct advice_value<advice::DontDump>   : std::integral_constant<int, MADV_DONTDUMP> {};

template <typename Advice>
inline constexpr int advice_value_v = advice_value<Advice>::value;

// `is_dangerous_advice_v<A>` — true iff A zeros/discards pages
// (currently {DontNeed}; future variants of MADV_FREE that the kernel
// chooses to dispatch as zero-now rather than zero-lazily would join
// this set, hence the named predicate over std::is_same_v).
//
// The safe `advise<>` surface uses `!is_dangerous_advice_v` as its
// concept gate; `advise_release_aware<>` uses the positive case.

template <typename Advice>
inline constexpr bool is_dangerous_advice_v =
    std::is_same_v<Advice, advice::DontNeed>;

}  // namespace crucible::fixy::mmap

// ═════════════════════════════════════════════════════════════════════
// ── grant tags — every mmap grant routes to SyscallSurface ────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per Grant.h's namespace-purity discipline (CR-09), all `which_dim`
// specializations MUST live syntactically inside
// `namespace crucible::fixy::grant`.  This header reopens that
// namespace; check-fixy-grant-namespace-purity.sh allowlists Mmap.h
// alongside Fs.h / Fp.h / syscall/*.

namespace crucible::fixy::grant {

namespace mmap {

// Each parametric grant takes a type-tag NTTP from the mmap:: enums.
// EBO-collapsible (sizeof == 1 standalone, 0 inside aggregators).

template <typename Prot>
struct with_prot       final : grant_base {};

template <typename Share>
struct with_share      final : grant_base {};

template <typename Advice>
struct with_advice     final : grant_base {};

// Trusted-JIT gate — enables `with_prot<prot::Exec>` at mint time.
// Documentary intent: caller has audited the executable bytes and
// asserts the W^X discipline (separate write-mapped region, code
// signing, RX-only at execution time).

struct trusted_jit     final : grant_base {};

// `release_aware<RegionTag>` — typed witness for the Bug 5 closure.
//
// V-225 ships the TYPE (compile-time gate at advise_release_aware<>
// requires this grant in the pack); V-234 will compose it with
// SharedPermissionPool<RegionTag> so the runtime proof of "no live
// ReadView<RegionTag>" is also enforced.  The RegionTag identifies
// WHICH region's ReadViews matter — the typed-permission machinery
// (SEPLOG/CSL) ensures cross-tag traffic can't masquerade.

template <typename RegionTag>
struct release_aware   final : grant_base {};

}  // namespace mmap

// ── which_dim routing ────────────────────────────────────────────────

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
struct which_dim<mmap::trusted_jit>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename RegionTag>
struct which_dim<mmap::release_aware<RegionTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── OwnedMmap + concept gates + mints + advise ────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::mmap {

// ── detail::* — grant-pack walkers ───────────────────────────────────

namespace detail {

// Extract Prot from with_prot<Prot> grant.  Non-with_prot grants give void.
template <typename G>
struct extract_prot { using type = void; };
template <typename Prot>
struct extract_prot<::crucible::fixy::grant::mmap::with_prot<Prot>> { using type = Prot; };
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

// `is_primary_with_share_v<G>` — G is `with_share<Share>` AND Share is
// one of the primary tiers (Private/Shared/Anonymous).  Used to count
// engagement on the primary axis (additive Locked/Populate/HugeTLB do
// NOT count toward this).
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

// Pack predicates — fold over Grants...

template <typename... Grants>
inline constexpr bool has_prot_grant_v = (is_with_prot_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_primary_share_grant_v = (is_primary_with_share_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_trusted_jit_v = (is_trusted_jit_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_release_aware_v = (is_release_aware_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_duplicate_prot_v =
    (static_cast<int>(is_with_prot_v<Grants>) + ...) > 1;

template <typename... Grants>
inline constexpr bool has_duplicate_primary_share_v =
    (static_cast<int>(is_primary_with_share_v<Grants>) + ...) > 1;

// Extract Prot from the Grants pack.  Walks the pack and returns the
// first with_prot<X>'s X.  Returns void if no with_prot is present
// (caller's concept gate should have caught this earlier).
template <typename... Grants>
struct prot_of;
template <typename First, typename... Rest>
struct prot_of<First, Rest...> {
    using type = std::conditional_t<
        is_with_prot_v<First>,
        extract_prot_t<First>,
        typename prot_of<Rest...>::type>;
};
template <>
struct prot_of<> { using type = void; };
template <typename... Grants>
using prot_of_t = typename prot_of<Grants...>::type;

// Same for Share — walks the pack, returns the first primary
// with_share<X>'s X.
template <typename G>
struct extract_share { using type = void; };
template <typename Share>
struct extract_share<::crucible::fixy::grant::mmap::with_share<Share>> { using type = Share; };
template <typename G>
using extract_share_t = typename extract_share<G>::type;

template <typename... Grants>
struct primary_share_of;
template <typename First, typename... Rest>
struct primary_share_of<First, Rest...> {
    using type = std::conditional_t<
        is_primary_with_share_v<First>,
        extract_share_t<First>,
        typename primary_share_of<Rest...>::type>;
};
template <>
struct primary_share_of<> { using type = void; };
template <typename... Grants>
using primary_share_of_t = typename primary_share_of<Grants...>::type;

// Pack-wide prot bits — OR of every with_prot<X>'s prot_bits_v.
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

// Pack-wide share flags — OR of every with_share<X>'s share_flags_v.
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

// `has_exec_prot_v<Grants...>` — pack contains `with_prot<prot::Exec>`.
template <typename G>
struct is_exec_prot : std::false_type {};
template <>
struct is_exec_prot<::crucible::fixy::grant::mmap::with_prot<prot::Exec>> : std::true_type {};
template <typename G>
inline constexpr bool is_exec_prot_v = is_exec_prot<G>::value;

template <typename... Grants>
inline constexpr bool has_exec_prot_v = (is_exec_prot_v<Grants> || ...);

// `pack_has_anonymous_v<Grants...>` — at least one
// `with_share<share::Anonymous>` in the pack.
template <typename G>
struct is_anonymous_share : std::false_type {};
template <>
struct is_anonymous_share<::crucible::fixy::grant::mmap::with_share<share::Anonymous>> : std::true_type {};
template <typename G>
inline constexpr bool is_anonymous_share_v = is_anonymous_share<G>::value;

template <typename... Grants>
inline constexpr bool pack_has_anonymous_v = (is_anonymous_share_v<Grants> || ...);

}  // namespace detail

// ── OwnedMmap<Tag, Prot, Share> — Linear RAII region ─────────────────
//
// Move-only RAII over a mmap'd region.  Destructor calls ::munmap if
// `is_mapped()` (the addr_ != MAP_FAILED && addr_ != nullptr predicate).
// Move semantics swap the carrier to MAP_FAILED so the moved-from
// instance no longer claims the region.
//
// V-231 will promote this to safety/OwnedMmap.h with the same shape;
// the fixy substrate keeps it here for V-225's call-site convenience.

template <typename Tag, typename Prot, typename Share>
class [[nodiscard]] OwnedMmap {
    void*       addr_ = MAP_FAILED;
    std::size_t len_  = 0;

public:
    using tag_type   = Tag;
    using prot_type  = Prot;
    using share_type = Share;

    OwnedMmap() noexcept = default;

    explicit OwnedMmap(void* address, std::size_t length) noexcept
        : addr_{address}, len_{length} {}

    OwnedMmap(const OwnedMmap&)            = delete("mmap region is unique; copy would double-unmap");
    OwnedMmap& operator=(const OwnedMmap&) = delete("mmap region is unique; copy would double-unmap");

    OwnedMmap(OwnedMmap&& other) noexcept
        : addr_{std::exchange(other.addr_, MAP_FAILED)},
          len_ {std::exchange(other.len_, 0)} {}

    OwnedMmap& operator=(OwnedMmap&& other) noexcept {
        if (this != &other) {
            release_();
            addr_ = std::exchange(other.addr_, MAP_FAILED);
            len_  = std::exchange(other.len_, 0);
        }
        return *this;
    }

    ~OwnedMmap() noexcept { release_(); }

    [[nodiscard]] void*       data()      const noexcept { return addr_; }
    [[nodiscard]] std::size_t size()      const noexcept { return len_; }
    [[nodiscard]] bool        is_mapped() const noexcept {
        return addr_ != MAP_FAILED && addr_ != nullptr;
    }

private:
    void release_() noexcept {
        if (is_mapped()) {
            ::munmap(addr_, len_);
            addr_ = MAP_FAILED;
            len_  = 0;
        }
    }
};

// ── §XXI ctx-bound mint gates — single-concept requires per family ───
//
// `CtxAdmitsIoBlock<Ctx>` mirrors fixy::fs::CtxAdmitsIoBlock: mmap and
// munmap can both park the caller (kernel page-cache pressure, NUMA
// remote-page faulting, write-back stalls), so we treat them as IO+Block
// like every other filesystem-touching syscall.

template <typename Ctx>
concept CtxAdmitsIoBlock =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<
           ::crucible::effects::row_type_of_t<Ctx>,
           ::crucible::effects::Effect::IO>
    && ::crucible::effects::row_contains_v<
           ::crucible::effects::row_type_of_t<Ctx>,
           ::crucible::effects::Effect::Block>;

// `CtxFitsMmapMint<Ctx, Grants...>` — single soundness gate on
// `mint_mmap<Tag, Grants...>(ctx, ...)`.  Bundles:
//
//   (1) Ctx is a valid ExecCtx admitting IO + Block effects.
//   (2) Grants pack engages exactly one with_prot<X> (rule 1, no dup).
//   (3) Grants pack engages exactly one primary with_share<X>
//       (Private/Shared/Anonymous; no dup).
//   (4) If pack engages with_prot<Exec>, it MUST also engage
//       trusted_jit (W^X gating; fixture #2).
//
// Mismatch on any rule fires a distinct HS14 fixture.

template <typename Ctx, typename... Grants>
concept CtxFitsMmapMint =
    CtxAdmitsIoBlock<Ctx>
    && detail::has_prot_grant_v<Grants...>
    && detail::has_primary_share_grant_v<Grants...>
    && !detail::has_duplicate_prot_v<Grants...>
    && !detail::has_duplicate_primary_share_v<Grants...>
    && (!detail::has_exec_prot_v<Grants...> || detail::has_trusted_jit_v<Grants...>);

// `CtxFitsAnonMmapMint<Ctx, Grants...>` — strict superset of
// CtxFitsMmapMint that additionally requires the primary share to be
// Anonymous (no file backing).  fixture #5 fires when this is violated.

template <typename Ctx, typename... Grants>
concept CtxFitsAnonMmapMint =
    CtxFitsMmapMint<Ctx, Grants...>
    && detail::pack_has_anonymous_v<Grants...>;

// `CtxFitsSafeAdvise<Ctx, Advice>` — gate for the safe-surface
// `advise<Advice>` call.  Refuses Advice ∈ dangerous-set (fixture #7).

template <typename Ctx, typename Advice>
concept CtxFitsSafeAdvise =
    CtxAdmitsIoBlock<Ctx>
    && !is_dangerous_advice_v<Advice>;

// `CtxFitsReleaseAwareAdvise<Ctx, Advice, RegionTag>` — gate for the
// dangerous-surface call.  Requires Advice ∈ dangerous-set + Ctx fit.
// (V-234 will fold a SharedPermission<RegionTag> consume into this
// concept, making the proof obligation runtime-witnessable.)

template <typename Ctx, typename Advice, typename RegionTag>
concept CtxFitsReleaseAwareAdvise =
    CtxAdmitsIoBlock<Ctx>
    && is_dangerous_advice_v<Advice>;

// ── mint_mmap<Tag, Grants...>(ctx, fd, length, offset) ───────────────
//
// §XXI ctx-bound mint.  File-backed mapping over an open file
// descriptor (passed as raw int rather than safety::FileHandle so the
// caller can use both Linux fd and shm_open results without conversion;
// the caller's FileHandle / Dirfd / shm fd must remain live for the
// region's lifetime — `MAP_SHARED` semantics).
//
// The returned OwnedMmap<Tag, Prot, Share> infers Prot and Share from
// the Grants pack via prot_of_t / primary_share_of_t.  Wrapped in
// safety::Linear<> at the boundary so callers can't accidentally
// discard the [[nodiscard]] result.

template <typename Tag, typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsMmapMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<OwnedMmap<Tag,
                                         detail::prot_of_t<Grants...>,
                                         detail::primary_share_of_t<Grants...>>>,
    std::error_code>
mint_mmap(Ctx const&,
          int          fd,
          std::size_t  length,
          ::off_t      offset = 0) noexcept {
    constexpr int prot_bits   = detail::fold_prot_bits<Grants...>();
    constexpr int share_flags = detail::fold_share_flags<Grants...>();
    void* const addr = ::mmap(nullptr, length, prot_bits, share_flags, fd, offset);
    if (addr == MAP_FAILED) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    using Region = OwnedMmap<Tag,
                             detail::prot_of_t<Grants...>,
                             detail::primary_share_of_t<Grants...>>;
    return ::crucible::safety::Linear<Region>{Region{addr, length}};
}

// ── mint_mmap_anon<Tag, Grants...>(ctx, length) ──────────────────────
//
// §XXI ctx-bound mint for anonymous mappings.  fd = -1 per the mmap(2)
// convention; offset is forced to 0.  Concept gate requires
// `with_share<share::Anonymous>` in the Grants pack — passing
// Shared or Private here fires fixture #5.

template <typename Tag, typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAnonMmapMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Linear<OwnedMmap<Tag,
                                         detail::prot_of_t<Grants...>,
                                         detail::primary_share_of_t<Grants...>>>,
    std::error_code>
mint_mmap_anon(Ctx const&,
               std::size_t length) noexcept {
    constexpr int prot_bits   = detail::fold_prot_bits<Grants...>();
    constexpr int share_flags = detail::fold_share_flags<Grants...>();
    void* const addr = ::mmap(nullptr, length, prot_bits, share_flags, -1, 0);
    if (addr == MAP_FAILED) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    using Region = OwnedMmap<Tag,
                             detail::prot_of_t<Grants...>,
                             detail::primary_share_of_t<Grants...>>;
    return ::crucible::safety::Linear<Region>{Region{addr, length}};
}

// ── advise<Advice>(ctx, OwnedMmap&) — safe surface ───────────────────
//
// NOT a §XXI mint — performs madvise on an existing region.  Concept
// gate refuses the dangerous-set; callers needing DontNeed must route
// through advise_release_aware<Advice, RegionTag>.

template <typename Advice,
          typename Tag, typename Prot, typename Share,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSafeAdvise<Ctx, Advice>
[[nodiscard]] inline std::expected<void, std::error_code>
advise(Ctx const&,
       OwnedMmap<Tag, Prot, Share>& region) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>) < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// ── advise_release_aware<Advice, RegionTag>(ctx, OwnedMmap&) ──────────
//
// Bug 5 narrow surface.  Compile-time witness that the caller has
// declared release-awareness for `RegionTag`.  V-234 will fold a
// SharedPermission<RegionTag> consume into this signature so the
// runtime proof "no live ReadView<RegionTag>" is enforced.

template <typename Advice, typename RegionTag,
          typename Tag, typename Prot, typename Share,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsReleaseAwareAdvise<Ctx, Advice, RegionTag>
[[nodiscard]] inline std::expected<void, std::error_code>
advise_release_aware(Ctx const&,
                     OwnedMmap<Tag, Prot, Share>& region) noexcept {
    if (!region.is_mapped()) {
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    }
    if (::madvise(region.data(), region.size(), advice_value_v<Advice>) < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests — pin the substrate at compile time ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

// ── prot_bits / share_flags / advice_value tables sane ───────────────
static_assert(prot_bits_v<prot::ReadOnly>  == PROT_READ);
static_assert(prot_bits_v<prot::WriteCopy> == (PROT_READ | PROT_WRITE));
static_assert(prot_bits_v<prot::ReadWrite> == (PROT_READ | PROT_WRITE));
static_assert(prot_bits_v<prot::Exec>      == (PROT_READ | PROT_EXEC));
static_assert((prot_bits_v<prot::Exec> & PROT_WRITE) == 0,
              "W^X: prot::Exec must NOT include PROT_WRITE");

static_assert(share_flags_v<share::Private>   == MAP_PRIVATE);
static_assert(share_flags_v<share::Shared>    == MAP_SHARED);
static_assert((share_flags_v<share::Anonymous> & MAP_ANONYMOUS) != 0);

static_assert(advice_value_v<advice::DontNeed> == MADV_DONTNEED);
static_assert(advice_value_v<advice::HugePage> == MADV_HUGEPAGE);
static_assert(advice_value_v<advice::Free>     == MADV_FREE);

static_assert(is_dangerous_advice_v<advice::DontNeed>);
static_assert(!is_dangerous_advice_v<advice::HugePage>);
static_assert(!is_dangerous_advice_v<advice::Sequential>);

// ── primary_share predicate ──────────────────────────────────────────
static_assert(is_primary_share_v<share::Private>);
static_assert(is_primary_share_v<share::Shared>);
static_assert(is_primary_share_v<share::Anonymous>);
static_assert(!is_primary_share_v<share::Locked>);
static_assert(!is_primary_share_v<share::Populate>);
static_assert(!is_primary_share_v<share::HugeTLB>);

// ── detail::* pack predicates ────────────────────────────────────────
using G_RO_Priv = ::crucible::fixy::grant::mmap::with_prot<prot::ReadOnly>;
using G_RW_Shar = ::crucible::fixy::grant::mmap::with_share<share::Shared>;
using G_RW_Priv = ::crucible::fixy::grant::mmap::with_share<share::Private>;
using G_Anon    = ::crucible::fixy::grant::mmap::with_share<share::Anonymous>;
using G_Locked  = ::crucible::fixy::grant::mmap::with_share<share::Locked>;
using G_Exec    = ::crucible::fixy::grant::mmap::with_prot<prot::Exec>;
using G_Jit     = ::crucible::fixy::grant::mmap::trusted_jit;

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

// prot_of / primary_share_of extract the right type from the pack.
static_assert(std::is_same_v<detail::prot_of_t<G_RO_Priv, G_RW_Shar>, prot::ReadOnly>);
static_assert(std::is_same_v<detail::prot_of_t<G_RW_Shar, G_Exec, G_Jit>, prot::Exec>);
static_assert(std::is_same_v<detail::primary_share_of_t<G_RO_Priv, G_RW_Shar>, share::Shared>);
static_assert(std::is_same_v<detail::primary_share_of_t<G_RO_Priv, G_Anon, G_Locked>, share::Anonymous>);

// fold_prot_bits / fold_share_flags OR the contributions.
static_assert(detail::fold_prot_bits<G_RO_Priv, G_Exec, G_Jit>()
              == (PROT_READ | PROT_EXEC));  // RO contributes PROT_READ; Exec contributes PROT_READ|PROT_EXEC
static_assert(detail::fold_share_flags<G_RW_Shar, G_Locked>()
              == (MAP_SHARED | MAP_LOCKED));

// ── OwnedMmap layout discipline ──────────────────────────────────────

struct RegionA {};  // dummy Tag for self-test
using TestRegion = OwnedMmap<RegionA, prot::ReadOnly, share::Private>;

static_assert(!std::is_copy_constructible_v<TestRegion>,
              "OwnedMmap must be move-only — copy would double-unmap");
static_assert(!std::is_copy_assignable_v<TestRegion>);
static_assert(std::is_nothrow_move_constructible_v<TestRegion>);
static_assert(std::is_nothrow_move_assignable_v<TestRegion>);
static_assert(std::is_nothrow_default_constructible_v<TestRegion>);
static_assert(sizeof(TestRegion) == sizeof(void*) + sizeof(std::size_t),
              "OwnedMmap is exactly {addr, len} — no hidden padding");

// ── CtxFitsMmapMint membership smoke (positive case) ─────────────────
//
// TestRunnerCtx has Row<Test, Alloc, IO, Block> per ExecCtx.h, so it
// admits IO+Block.  G_RO_Priv + G_RW_Shar engages both axes once
// without duplicate, no Exec, no trusted_jit needed — gate accepts.

static_assert(CtxFitsMmapMint<
    ::crucible::effects::TestRunnerCtx,
    G_RO_Priv, G_RW_Shar>);

// Anon variant — TestRunnerCtx + ReadOnly + Anonymous (primary).
static_assert(CtxFitsAnonMmapMint<
    ::crucible::effects::TestRunnerCtx,
    G_RO_Priv, G_Anon>);

// Anon variant REJECTS Shared (must be Anonymous).
static_assert(!CtxFitsAnonMmapMint<
    ::crucible::effects::TestRunnerCtx,
    G_RO_Priv, G_RW_Shar>);

// Exec without trusted_jit REJECTS.
static_assert(!CtxFitsMmapMint<
    ::crucible::effects::TestRunnerCtx,
    G_Exec, G_RW_Priv>);
// Exec WITH trusted_jit accepts.
static_assert(CtxFitsMmapMint<
    ::crucible::effects::TestRunnerCtx,
    G_Exec, G_RW_Priv, G_Jit>);

// Empty Grants rejects (no prot, no share).
static_assert(!CtxFitsMmapMint<::crucible::effects::TestRunnerCtx>);

// Safe advise accepts non-dangerous advices.
static_assert(CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::HugePage>);
static_assert(CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::Sequential>);
// Safe advise REJECTS DontNeed (Bug 5 closure).
static_assert(!CtxFitsSafeAdvise<::crucible::effects::TestRunnerCtx, advice::DontNeed>);
// Release-aware advise accepts DontNeed.
static_assert(CtxFitsReleaseAwareAdvise<::crucible::effects::TestRunnerCtx, advice::DontNeed, RegionA>);
// Release-aware advise REJECTS non-dangerous advices (use safe surface).
static_assert(!CtxFitsReleaseAwareAdvise<::crucible::effects::TestRunnerCtx, advice::HugePage, RegionA>);

}  // namespace self_test

}  // namespace crucible::fixy::mmap
