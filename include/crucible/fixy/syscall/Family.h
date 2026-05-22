#pragma once

// ── crucible::fixy::grant::syscall — coarse family grants (V-098) ────
//
// Nine family-tier grant tags engaging `DimensionAxis::SyscallSurface`
// (substrate ordinal 23, V-097 2026-05-22).  Each tag pins ONE of the nine
// `algebra::lattices::SyscallFamily` chain-lattice tiers (V-097's
// algebra/lattices/SyscallFamilyLattice.h — the enum lives in the
// algebra layer because the lattice owns it; safety:: re-export
// arrives with V-100's `safety/SyscallSurface.h` wrapper):
//
//     family_no_syscall          — bottom (the binding makes NO syscalls)
//     family_vdso_only           — only vDSO-resolved (clock_gettime, getcpu)
//     family_read_only_state     — read-only metadata (getpid, uname, sysinfo)
//     family_file_mutation       — file-handle CRUD (open, read, write, close)
//     family_memory_mapping      — mmap-family (mmap, mprotect, madvise)
//     family_thread_sync         — sync primitives (futex, sched_setaffinity)
//     family_network_io          — socket-family (socket, bind, sendmsg)
//     family_process_control     — fork/exec/wait (clone, execve, wait4)
//     family_privilege           — top (capset, mount, ptrace, setuid)
//
// Composition law is Tier-S Semiring (par=join, strictest-wins): a
// binding declared at `family_file_mutation` composed with one at
// `family_network_io` joins at `max(FileMutation, NetworkIo)` =
// NetworkIo (NetworkIo is higher on the V-097 chain).  The hot-path
// admission gate uses this join to compute the composite syscall
// surface for cross-binding par sites.
//
// ── Why a separate header (rather than extending Grant.h) ─────────────
//
// Grant.h groups tags by SAFETY-LATTICE taxonomy (Security, Trust,
// Lifetime, ...).  SyscallSurface is a META-axis — it composes:
//
//   * NINE family tiers (this header, V-098 Family.h)
//   * N per-syscall identities (Per.h, V-098)
//   * Per-vendor ioctl operations (V-099 Ioctl.h, deferred)
//   * Automatic effect-row lift (V-100 Bridge.h, deferred)
//
// Keeping family-tier grants in `fixy/syscall/Family.h` gives the
// syscall taxonomy a single grep target (`grant::syscall::family_*`)
// and parallels `fixy/Fp.h`'s precedent (V-092 doc-block lines 17-35).
//
// Per Grant.h's namespace-purity discipline (lines 121-158), all
// `which_dim` specializations MUST live syntactically inside
// `namespace crucible::fixy::grant`.  This header reopens that
// namespace under the CR-09 allowlist alongside Fp.h and Per.h
// (scripts/check-fixy-grant-namespace-purity.sh).
//
// ── Substrate consumed ────────────────────────────────────────────────
//
//   crucible::fixy::dim::DimensionAxis        — enum cited by which_dim
//   crucible::fixy::grant::grant_base         — structural marker (Grant.h)
//   crucible::fixy::grant::which_dim          — primary template (Grant.h)
//   crucible::algebra::lattices::SyscallFamily        — V-097 chain enum
//   crucible::algebra::lattices::SyscallFamilyLattice — V-097 lattice
//
// ── Substrate added by this header ────────────────────────────────────
//
// NONE.  Every tag is `final + grant_base` empty struct, sizeof == 1
// standalone, EBO-collapsing to 0 inside any aggregator.  Each tag's
// `which_dim` specialization routes to `DimensionAxis::SyscallSurface`.
// No new lattice, no new wrapper, no new mint factory.
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  Every tag carries no runtime state; `which_dim_v<G>` is a
// single integral_constant member access at compile time.
//
// ── How the grants resolve inside fixy::fn<...> ───────────────────────
//
// V-097 marked SyscallSurface a federation/reflection axis.  Each
// grant in this header acts as the engagement marker (Reject.h's
// `every_axis_engaged` walk treats it like any other axis grant) AND
// carries a `family_tier_v<G>` metadata channel pointing at the
// associated SyscallFamily enumerator.  V-100's bridge will read
// `family_tier_v<G>` to lift the family into the Met(X) effect row.
//
// You pick ONE syscall-axis grant per binding: either a family-level
// grant (this header) OR a per-syscall grant (Per.h).  Combining both
// trips `FixyDuplicate_SyscallSurface` (Reject.h) — the cross-syscall-
// surface duplicate gate is the same machinery that catches
// `fp_strict_ieee + with_fp_rounding<X>` (V-092 neg fixture).
//
// ── Self-test ─────────────────────────────────────────────────────────
//
// Six load-bearing assertion families per tag (nine tags × six layers
// = 54 assertions, sampled exhaustively here):
//
//   1. IsGrantTag<G>   — final + grant_base + cv-ref-free
//   2. sizeof(G) == 1  — EBO-collapsible empty marker
//   3. which_dim_v<G>  — routes to DimensionAxis::SyscallSurface
//   4. family_tier_v<G> — metadata pin (every tag → matching family)
//   5. Pairwise type-distinctness — all 9 tags are distinct types
//   6. Family tier coverage — the 9 tags collectively pin every
//      SyscallFamily enumerator exactly once (bijection witness)
//
// HS14 negative-compile fixtures live in test/fixy_neg/
// neg_fixy_v_098_*.cpp witnessing (a) wrong-axis routing rejection
// and (b) family + per duplicate engagement rejection.

#include <crucible/fixy/Grant.h>           // grant_base, which_dim primary
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/algebra/lattices/SyscallFamilyLattice.h> // SyscallFamily

#include <type_traits>

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── family_tier_v<G> — per-tag metadata channel ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Maps a syscall-axis grant tag to its `algebra::lattices::SyscallFamily`
// tier (the enum's canonical home — safety:: re-export lands with V-100).
// Family-level tags (this header) map to their named tier directly;
// per-syscall tags (Per.h) map via the syscall's family classification
// (Per.h's `family_of(SyscallId)` switch).  V-100's bridge reads this
// trait to compute the row lift; downstream consumers compute the
// par=join composition over packs of bindings via this metadata
// without dispatching on the lattice value at runtime.

template <typename G>
struct family_tier;  // primary undefined — specialized per family tag

template <typename G>
inline constexpr ::crucible::algebra::lattices::SyscallFamily family_tier_v =
    family_tier<G>::value;

// ═════════════════════════════════════════════════════════════════════
// ── 9 family-tier grant tags ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace syscall {

// ── Tier 0: NoSyscall (bottom — the binding makes NO syscalls) ──────
struct family_no_syscall final : grant_base {};

// ── Tier 1: VdsoOnly (only vDSO-resolved calls) ─────────────────────
struct family_vdso_only final : grant_base {};

// ── Tier 2: ReadOnlyState (read-only metadata queries) ──────────────
struct family_read_only_state final : grant_base {};

// ── Tier 3: FileMutation (file-handle CRUD) ──────────────────────────
struct family_file_mutation final : grant_base {};

// ── Tier 4: MemoryMapping (mmap-family) ──────────────────────────────
struct family_memory_mapping final : grant_base {};

// ── Tier 5: ThreadSync (sync primitives) ─────────────────────────────
struct family_thread_sync final : grant_base {};

// ── Tier 6: NetworkIo (socket-family) ────────────────────────────────
struct family_network_io final : grant_base {};

// ── Tier 7: ProcessControl (fork/exec/wait) ──────────────────────────
struct family_process_control final : grant_base {};

// ── Tier 8: Privilege (top — capset, mount, ptrace, setuid) ──────────
struct family_privilege final : grant_base {};

}  // namespace syscall

// ── which_dim routing (every family tag → SyscallSurface) ───────────
template <>
struct which_dim<syscall::family_no_syscall>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_vdso_only>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_read_only_state>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_file_mutation>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_memory_mapping>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_thread_sync>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_network_io>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_process_control>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <>
struct which_dim<syscall::family_privilege>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

// ── family_tier specializations (every family tag → matching tier) ──
template <>
struct family_tier<syscall::family_no_syscall>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::NoSyscall> {};
template <>
struct family_tier<syscall::family_vdso_only>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::VdsoOnly> {};
template <>
struct family_tier<syscall::family_read_only_state>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ReadOnlyState> {};
template <>
struct family_tier<syscall::family_file_mutation>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::FileMutation> {};
template <>
struct family_tier<syscall::family_memory_mapping>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::MemoryMapping> {};
template <>
struct family_tier<syscall::family_thread_sync>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ThreadSync> {};
template <>
struct family_tier<syscall::family_network_io>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::NetworkIo> {};
template <>
struct family_tier<syscall::family_process_control>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::ProcessControl> {};
template <>
struct family_tier<syscall::family_privilege>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::syscall_family_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D      = dim::DimensionAxis;
using SF     = al::SyscallFamily;

// ── Layer 1: IsGrantTag — every family tag structurally valid ──────
static_assert(IsGrantTag<sc::family_no_syscall>);
static_assert(IsGrantTag<sc::family_vdso_only>);
static_assert(IsGrantTag<sc::family_read_only_state>);
static_assert(IsGrantTag<sc::family_file_mutation>);
static_assert(IsGrantTag<sc::family_memory_mapping>);
static_assert(IsGrantTag<sc::family_thread_sync>);
static_assert(IsGrantTag<sc::family_network_io>);
static_assert(IsGrantTag<sc::family_process_control>);
static_assert(IsGrantTag<sc::family_privilege>);

// ── Layer 2: sizeof — 1 byte standalone, EBO-collapsible ───────────
static_assert(sizeof(sc::family_no_syscall)       == 1);
static_assert(sizeof(sc::family_vdso_only)        == 1);
static_assert(sizeof(sc::family_read_only_state)  == 1);
static_assert(sizeof(sc::family_file_mutation)    == 1);
static_assert(sizeof(sc::family_memory_mapping)   == 1);
static_assert(sizeof(sc::family_thread_sync)      == 1);
static_assert(sizeof(sc::family_network_io)       == 1);
static_assert(sizeof(sc::family_process_control)  == 1);
static_assert(sizeof(sc::family_privilege)        == 1);

// ── Layer 3: which_dim routing — every tag → SyscallSurface ────────
static_assert(which_dim_v<sc::family_no_syscall>       == D::SyscallSurface);
static_assert(which_dim_v<sc::family_vdso_only>        == D::SyscallSurface);
static_assert(which_dim_v<sc::family_read_only_state>  == D::SyscallSurface);
static_assert(which_dim_v<sc::family_file_mutation>    == D::SyscallSurface);
static_assert(which_dim_v<sc::family_memory_mapping>   == D::SyscallSurface);
static_assert(which_dim_v<sc::family_thread_sync>      == D::SyscallSurface);
static_assert(which_dim_v<sc::family_network_io>       == D::SyscallSurface);
static_assert(which_dim_v<sc::family_process_control>  == D::SyscallSurface);
static_assert(which_dim_v<sc::family_privilege>        == D::SyscallSurface);

// ── Layer 4: family_tier — every tag → matching SyscallFamily ──────
static_assert(family_tier_v<sc::family_no_syscall>       == SF::NoSyscall);
static_assert(family_tier_v<sc::family_vdso_only>        == SF::VdsoOnly);
static_assert(family_tier_v<sc::family_read_only_state>  == SF::ReadOnlyState);
static_assert(family_tier_v<sc::family_file_mutation>    == SF::FileMutation);
static_assert(family_tier_v<sc::family_memory_mapping>   == SF::MemoryMapping);
static_assert(family_tier_v<sc::family_thread_sync>      == SF::ThreadSync);
static_assert(family_tier_v<sc::family_network_io>       == SF::NetworkIo);
static_assert(family_tier_v<sc::family_process_control>  == SF::ProcessControl);
static_assert(family_tier_v<sc::family_privilege>        == SF::Privilege);

// ── Layer 5: pairwise type-distinctness — sampled adjacencies ──────
// Each pair witnesses the V-097 chain monotonicity at the type level:
// adjacent tiers are distinct types so packs cannot accidentally
// collapse to a single engagement when two tags appear in different
// positions.  Sentinel TU runs the full 9×8/2 = 36-cell matrix.
static_assert(!std::is_same_v<sc::family_no_syscall,      sc::family_vdso_only>);
static_assert(!std::is_same_v<sc::family_vdso_only,       sc::family_read_only_state>);
static_assert(!std::is_same_v<sc::family_read_only_state, sc::family_file_mutation>);
static_assert(!std::is_same_v<sc::family_file_mutation,   sc::family_memory_mapping>);
static_assert(!std::is_same_v<sc::family_memory_mapping,  sc::family_thread_sync>);
static_assert(!std::is_same_v<sc::family_thread_sync,     sc::family_network_io>);
static_assert(!std::is_same_v<sc::family_network_io,      sc::family_process_control>);
static_assert(!std::is_same_v<sc::family_process_control, sc::family_privilege>);
// Cross-chain (non-adjacent) sample
static_assert(!std::is_same_v<sc::family_no_syscall,      sc::family_privilege>);

// ── Layer 6: family-tier coverage — bijection witness ──────────────
// The nine tags collectively cover the nine SyscallFamily tiers
// exactly once.  We witness via a covering predicate: every tier
// value appears as the family_tier of at least one tag.  Combined
// with the cardinality assertion below, this proves bijection.
[[nodiscard]] consteval bool every_family_tier_covered() noexcept {
    using sf2 = al::SyscallFamily;
    return family_tier_v<sc::family_no_syscall>      == sf2::NoSyscall
        && family_tier_v<sc::family_vdso_only>       == sf2::VdsoOnly
        && family_tier_v<sc::family_read_only_state> == sf2::ReadOnlyState
        && family_tier_v<sc::family_file_mutation>   == sf2::FileMutation
        && family_tier_v<sc::family_memory_mapping>  == sf2::MemoryMapping
        && family_tier_v<sc::family_thread_sync>     == sf2::ThreadSync
        && family_tier_v<sc::family_network_io>      == sf2::NetworkIo
        && family_tier_v<sc::family_process_control> == sf2::ProcessControl
        && family_tier_v<sc::family_privilege>       == sf2::Privilege;
}
static_assert(every_family_tier_covered(),
    "FIXY-V-098: nine family tags MUST collectively cover the nine "
    "SyscallFamily tiers exactly once.  If this fires, the family_tier "
    "specialization for at least one tag is wrong or missing.");

// Cross-check against V-097's cardinality assertion.
static_assert(
    ::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
    "FIXY-V-098 depends on V-097's 9-tier chain.  If the lattice grew "
    "an enumerator, every family_tier specialization above is stale.");

}  // namespace detail::syscall_family_grant_self_test

}  // namespace crucible::fixy::grant
