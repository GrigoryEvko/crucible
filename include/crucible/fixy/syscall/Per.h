#pragma once

// ── crucible::fixy::grant::syscall — per-syscall grants (V-098, file 2/2) ─
//
// Parametric grant `per<SyscallId>` engaging `DimensionAxis::SyscallSurface`
// (V-097 dim 23, V-098 Family.h ships the coarse family-tier siblings).
// Each instantiation of `per<Id>` is a distinct 1-byte type pinning ONE
// individual syscall on the V-097 9-tier chain; the per-syscall family
// classification flows through the same `family_tier_v<G>` metadata
// channel Family.h defines, computed by `family_of(SyscallId)` at compile
// time.
//
// Pattern reference: V-092 `fixy/Fp.h`'s `with_fp_rounding<FpRounding>`
// (lines 122-127) — parametric grant whose NTTP discriminates the
// instantiation while a single `which_dim` template route all
// instantiations to the same DimensionAxis.  The structural difference
// here is that the parametric class (`per`) lives in a NESTED namespace
// (`syscall::`) to keep the top-level `crucible::fixy::grant` namespace
// from getting flooded with ~36 syscall identifiers, AND because the
// SyscallId enum itself wants to live near its consumers (the syscall
// taxonomy is V-097's domain).
//
// ── Catalog scope — 36 SyscallId enumerators ──────────────────────────
//
// One enumerator per representative syscall per family-tier — enough
// breadth to drive Forge phase E.RecipeSelect's "may a kernel touch
// kernel-mode at all?" gate, the Cipher tier promotion's tier-class
// discrimination, and the Canopy peer-RX path's family-aware admission.
// The catalog is APPEND-ONLY per FOUND-I04 Universe extension:
// later additions (V-099 ioctl variants, V-100 bridge convenience tags)
// MUST insert at the next free ordinal — never reuse an existing one,
// never reorder.  Reusing an ordinal silently changes every stored
// row_hash (federation cache key) without warning.
//
// ── Family classification via family_of(SyscallId) ───────────────────
//
// Each SyscallId resolves at compile time to ONE SyscallFamily via the
// `family_of(SyscallId)` switch.  The switch is the load-bearing
// authority — `per<Id>` derives its `family_tier_v<>` directly from
// `family_of(Id)` so a binding's family-tier metadata stays consistent
// whether the caller engages SyscallSurface via `family_thread_sync`
// (Family.h) or via `per<SyscallId::futex>` (this header).
//
// V-100's bridge consumes `family_tier_v<G>` to lift the syscall surface
// into the Met(X) effect row automatically; both Family.h and Per.h
// participate transparently because they share the metadata channel.
//
// ── Self-test (compile-time + runtime smoke) ─────────────────────────
//
// Seven layers per the doc-block:
//
//   1. IsGrantTag<per<Id>>           — final + grant_base + cv-ref-free
//   2. sizeof(per<Id>) == 1          — EBO-collapsible empty marker
//   3. which_dim_v<per<Id>>          — routes to SyscallSurface
//   4. family_tier_v<per<Id>>        — matches family_of(Id)
//   5. NTTP-distinctness             — distinct Ids → distinct types
//   6. Cross-axis distinctness       — per<Id> ≠ Family.h's family_* tags
//   7. family_of() coverage          — every SyscallId resolves to a
//                                      non-sentinel SyscallFamily
//
// HS14 negative-compile fixtures live in test/fixy_neg/
// neg_fixy_v_098_*.cpp witnessing (a) wrong-axis routing rejection
// (a contributor accidentally specializes `which_dim<per<Id>>` to a
// non-SyscallSurface axis), and (b) family + per duplicate engagement
// rejection (a binding pack contains BOTH `family_file_mutation` and
// `per<SyscallId::write>` — both engage SyscallSurface).
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  Every `per<Id>` is a 1-byte empty struct; `family_tier_v<>`
// resolves at compile time via the constexpr `family_of()` switch.
// `family_of()` itself is `[[gnu::pure]]` and consteval-eligible — a
// caller writing `family_tier_v<per<SyscallId::futex>>` materializes
// `SyscallFamily::ThreadSync` with zero runtime cost.

#include <crucible/fixy/syscall/Family.h>   // family_tier primary + syscall ns
#include <crucible/fixy/Grant.h>            // grant_base + which_dim primary
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/algebra/lattices/SyscallFamilyLattice.h> // SyscallFamily

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::grant::syscall {

// ═════════════════════════════════════════════════════════════════════
// ── SyscallId — append-only catalog of 36 representative syscalls ────
// ═════════════════════════════════════════════════════════════════════
//
// Enumerator NUMBERS are stable forever (FOUND-I04 append-only).  Names
// match the Linux syscall convention (lower-snake-case, no `sys_`
// prefix) — V-099's ioctl additions will reuse this naming convention
// at the next free ordinal.
//
// Ordering groups by family-tier (NoSyscall → Privilege bottom-to-top
// on V-097's chain) for human readability; the ordinal IS the
// authoritative key, the grouping is mnemonic only.
enum class SyscallId : std::uint16_t {
    // ── VdsoOnly (vDSO-resolved, no kernel transition) ─────────────
    clock_gettime    = 0,   // CLOCK_MONOTONIC/REALTIME read via vDSO
    clock_getres     = 1,   // resolution query, vDSO path
    getcpu_vdso      = 2,   // CPU+NUMA-node, vDSO path
    gettimeofday     = 3,   // legacy clock read, vDSO path

    // ── ReadOnlyState (read-only kernel queries) ───────────────────
    getpid           = 4,   // process ID query
    getppid          = 5,   // parent process ID query
    getuid           = 6,   // user ID query
    geteuid          = 7,   // effective user ID query
    getgid           = 8,   // group ID query
    gettid           = 9,   // thread ID query
    uname            = 10,  // system info query
    sysinfo          = 11,  // memory + load query

    // ── FileMutation (file-handle CRUD) ────────────────────────────
    open             = 12,  // legacy file open
    openat           = 13,  // dirfd-relative open (preferred)
    close            = 14,  // file close
    read             = 15,  // sequential read
    write            = 16,  // sequential write
    pread            = 17,  // positioned read
    pwrite           = 18,  // positioned write (Cipher cold-tier hot path)
    fsync            = 19,  // full sync (data + metadata)
    fdatasync        = 20,  // data-only sync (Cipher durability path)

    // ── MemoryMapping (mmap-family) ────────────────────────────────
    mmap             = 21,  // virtual mapping create
    munmap           = 22,  // virtual mapping release
    mprotect         = 23,  // permission change on mapped region
    madvise          = 24,  // kernel advice on mapped region

    // ── ThreadSync (sync primitives) ───────────────────────────────
    futex            = 25,  // userspace mutex backing
    sched_yield      = 26,  // voluntary preemption
    sched_setaffinity = 27, // CPU pinning

    // ── NetworkIo (socket-family) ──────────────────────────────────
    socket           = 28,  // socket create
    connect          = 29,  // outbound connection establish
    sendmsg          = 30,  // scatter-gather send
    recvmsg          = 31,  // scatter-gather receive

    // ── ProcessControl (fork/exec/wait) ────────────────────────────
    clone            = 32,  // process/thread spawn (CLONE_THREAD vs PROC)
    execve           = 33,  // exec replacement

    // ── Privilege (top — capset, mount, ptrace) ────────────────────
    ptrace           = 34,  // process trace (mutating)
    capset           = 35,  // capability set (drop or grant)

    // ── Append-only V-180 extensions (warden/Hardening.h surface) ──
    // Five additional syscalls the warden hardening path issues.
    // Ordinals 36-40 frozen forever per the V-098 federation-cache
    // stability invariant.
    sched_setattr    = 36,  // scheduler attribute change (SCHED_FIFO etc., ThreadSync)
    mlock2           = 37,  // page locking with flags (MemoryMapping)
    mlock            = 38,  // page locking (MemoryMapping)
    munlock          = 39,  // page unlocking (MemoryMapping)
    prctl            = 40,  // process control (PR_SET_THP_DISABLE etc., Privilege)
};

// ── family_of(SyscallId) — the load-bearing classifier ─────────────
//
// Maps each SyscallId to its V-097 SyscallFamily tier.  The switch is
// EXHAUSTIVE — every SyscallId enumerator has a corresponding arm, and
// the default returns a sentinel that the self-test below witnesses
// cannot be returned for any shipped enumerator.
//
// V-100's bridge will read this through `family_tier_v<per<Id>>`; the
// reading is consteval-eligible so the bridge's effect-row lift is
// zero-cost at the call site.
[[nodiscard]] constexpr ::crucible::algebra::lattices::SyscallFamily
family_of(SyscallId id) noexcept {
    using SF = ::crucible::algebra::lattices::SyscallFamily;
    switch (id) {
        // VdsoOnly
        case SyscallId::clock_gettime:     return SF::VdsoOnly;
        case SyscallId::clock_getres:      return SF::VdsoOnly;
        case SyscallId::getcpu_vdso:       return SF::VdsoOnly;
        case SyscallId::gettimeofday:      return SF::VdsoOnly;

        // ReadOnlyState
        case SyscallId::getpid:            return SF::ReadOnlyState;
        case SyscallId::getppid:           return SF::ReadOnlyState;
        case SyscallId::getuid:            return SF::ReadOnlyState;
        case SyscallId::geteuid:           return SF::ReadOnlyState;
        case SyscallId::getgid:            return SF::ReadOnlyState;
        case SyscallId::gettid:            return SF::ReadOnlyState;
        case SyscallId::uname:             return SF::ReadOnlyState;
        case SyscallId::sysinfo:           return SF::ReadOnlyState;

        // FileMutation
        case SyscallId::open:              return SF::FileMutation;
        case SyscallId::openat:            return SF::FileMutation;
        case SyscallId::close:             return SF::FileMutation;
        case SyscallId::read:              return SF::FileMutation;
        case SyscallId::write:             return SF::FileMutation;
        case SyscallId::pread:             return SF::FileMutation;
        case SyscallId::pwrite:            return SF::FileMutation;
        case SyscallId::fsync:             return SF::FileMutation;
        case SyscallId::fdatasync:         return SF::FileMutation;

        // MemoryMapping
        case SyscallId::mmap:              return SF::MemoryMapping;
        case SyscallId::munmap:            return SF::MemoryMapping;
        case SyscallId::mprotect:          return SF::MemoryMapping;
        case SyscallId::madvise:           return SF::MemoryMapping;

        // ThreadSync
        case SyscallId::futex:             return SF::ThreadSync;
        case SyscallId::sched_yield:       return SF::ThreadSync;
        case SyscallId::sched_setaffinity: return SF::ThreadSync;
        case SyscallId::sched_setattr:     return SF::ThreadSync;

        // NetworkIo
        case SyscallId::socket:            return SF::NetworkIo;
        case SyscallId::connect:           return SF::NetworkIo;
        case SyscallId::sendmsg:           return SF::NetworkIo;
        case SyscallId::recvmsg:           return SF::NetworkIo;

        // ProcessControl
        case SyscallId::clone:             return SF::ProcessControl;
        case SyscallId::execve:            return SF::ProcessControl;

        // Privilege
        case SyscallId::ptrace:            return SF::Privilege;
        case SyscallId::capset:            return SF::Privilege;
        case SyscallId::prctl:             return SF::Privilege;

        // V-180 MemoryMapping additions — mem-locking is a mapping-state
        // modifier (locks pages into RAM, attribute of an existing mapping).
        case SyscallId::mlock2:            return SF::MemoryMapping;
        case SyscallId::mlock:             return SF::MemoryMapping;
        case SyscallId::munlock:           return SF::MemoryMapping;

        // Default — returned only if the switch is non-exhaustive,
        // which the self-test below witnesses cannot occur for any
        // shipped enumerator.  Returning NoSyscall would silently
        // misclassify a forgotten enumerator as the bottom of the
        // chain (the most permissive admission); returning Privilege
        // (top of the chain) is the least-surprising fallback (the
        // strictest possible classification) — still incorrect for a
        // forgotten enumerator, but at least it errs toward over-
        // restriction.  The `default:` arm is required by the project
        // `-Werror=switch-default` policy.
        default:                           return SF::Privilege;
    }
}

// ═════════════════════════════════════════════════════════════════════
// ── per<Id> — the parametric per-syscall grant ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One distinct empty marker type per SyscallId.  Final + inherits
// grant_base — passes IsGrantTag.  Instantiations with different Ids
// are distinct types (NTTP discrimination), so a pack engaging both
// `per<SyscallId::write>` and `per<SyscallId::read>` correctly
// witnesses TWO distinct grants on the SyscallSurface axis — and
// therefore (per Reject.h's duplicate-engagement gate) MUST be
// rejected with `FixyDuplicate_SyscallSurface`.
//
// The "engage one per binding" discipline is identical to FpMode (V-092):
// the SyscallSurface axis admits a SINGLE par=join engagement.  Multiple
// per<> tags or a per<> + family_* combination in the same Grants pack
// trips duplicate engagement on the same axis.
template <SyscallId Id>
struct per final : grant_base {};

}  // namespace crucible::fixy::grant::syscall

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── which_dim<per<Id>> — every per<> instantiation → SyscallSurface ──
// ═════════════════════════════════════════════════════════════════════
template <syscall::SyscallId Id>
struct which_dim<syscall::per<Id>>
    : std::integral_constant<dim::DimensionAxis,
                             dim::DimensionAxis::SyscallSurface> {};

// ═════════════════════════════════════════════════════════════════════
// ── family_tier<per<Id>> — derive family via family_of(Id) ───────────
// ═════════════════════════════════════════════════════════════════════
//
// V-098 Family.h declared the `family_tier` primary template (undefined
// primary).  This specialization derives each per<Id>'s family-tier
// from the canonical `family_of()` classifier; downstream consumers
// reading `family_tier_v<per<SyscallId::futex>>` materialize
// `SyscallFamily::ThreadSync` at compile time with zero runtime cost.
template <syscall::SyscallId Id>
struct family_tier<syscall::per<Id>>
    : std::integral_constant<
          ::crucible::algebra::lattices::SyscallFamily,
          syscall::family_of(Id)> {};

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Seven load-bearing assertion families.  Sampled at every family-tier
// (so each tier's classifier arm is exercised at least once); a
// reflection-driven exhaustive bijection witness covers the full
// 36-enumerator surface.

namespace detail::syscall_per_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D      = dim::DimensionAxis;
using SF     = al::SyscallFamily;
using SI     = sc::SyscallId;

// ── Layer 1: IsGrantTag — sampled at every family-tier ──────────────
static_assert(IsGrantTag<sc::per<SI::clock_gettime>>);   // VdsoOnly
static_assert(IsGrantTag<sc::per<SI::getpid>>);          // ReadOnlyState
static_assert(IsGrantTag<sc::per<SI::write>>);           // FileMutation
static_assert(IsGrantTag<sc::per<SI::mmap>>);            // MemoryMapping
static_assert(IsGrantTag<sc::per<SI::futex>>);           // ThreadSync
static_assert(IsGrantTag<sc::per<SI::socket>>);          // NetworkIo
static_assert(IsGrantTag<sc::per<SI::clone>>);           // ProcessControl
static_assert(IsGrantTag<sc::per<SI::ptrace>>);          // Privilege

// ── Layer 2: sizeof — 1 byte standalone, EBO-collapsible ─────────────
static_assert(sizeof(sc::per<SI::clock_gettime>)   == 1);
static_assert(sizeof(sc::per<SI::getpid>)          == 1);
static_assert(sizeof(sc::per<SI::write>)           == 1);
static_assert(sizeof(sc::per<SI::mmap>)            == 1);
static_assert(sizeof(sc::per<SI::futex>)           == 1);
static_assert(sizeof(sc::per<SI::socket>)          == 1);
static_assert(sizeof(sc::per<SI::clone>)           == 1);
static_assert(sizeof(sc::per<SI::ptrace>)          == 1);

// ── Layer 3: which_dim routing — every per<> → SyscallSurface ────────
static_assert(which_dim_v<sc::per<SI::clock_gettime>>   == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::getpid>>          == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::write>>           == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::mmap>>            == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::futex>>           == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::socket>>          == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::clone>>           == D::SyscallSurface);
static_assert(which_dim_v<sc::per<SI::ptrace>>          == D::SyscallSurface);

// ── Layer 4: family_tier — matches family_of(Id) at every tier ──────
static_assert(family_tier_v<sc::per<SI::clock_gettime>>     == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::clock_getres>>      == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::gettimeofday>>      == SF::VdsoOnly);
static_assert(family_tier_v<sc::per<SI::getpid>>            == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::geteuid>>           == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::sysinfo>>           == SF::ReadOnlyState);
static_assert(family_tier_v<sc::per<SI::open>>              == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::write>>             == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::pwrite>>            == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::fdatasync>>         == SF::FileMutation);
static_assert(family_tier_v<sc::per<SI::mmap>>              == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::mprotect>>          == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::madvise>>           == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::futex>>             == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::sched_yield>>       == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::sched_setaffinity>> == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::socket>>            == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::connect>>           == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::sendmsg>>           == SF::NetworkIo);
static_assert(family_tier_v<sc::per<SI::clone>>             == SF::ProcessControl);
static_assert(family_tier_v<sc::per<SI::execve>>            == SF::ProcessControl);
static_assert(family_tier_v<sc::per<SI::ptrace>>            == SF::Privilege);
static_assert(family_tier_v<sc::per<SI::capset>>            == SF::Privilege);

// V-180: warden/Hardening.h surface additions — exercise each new ordinal.
static_assert(family_tier_v<sc::per<SI::sched_setattr>>     == SF::ThreadSync);
static_assert(family_tier_v<sc::per<SI::mlock>>             == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::mlock2>>            == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::munlock>>           == SF::MemoryMapping);
static_assert(family_tier_v<sc::per<SI::prctl>>             == SF::Privilege);

// ── Layer 5: NTTP-distinctness — distinct Ids → distinct types ──────
// Sampled across every tier boundary; sentinel TU runs the full
// 36×35/2 = 630-cell distinctness matrix.
static_assert(!std::is_same_v<sc::per<SI::clock_gettime>,   sc::per<SI::getpid>>);
static_assert(!std::is_same_v<sc::per<SI::getpid>,          sc::per<SI::write>>);
static_assert(!std::is_same_v<sc::per<SI::write>,           sc::per<SI::mmap>>);
static_assert(!std::is_same_v<sc::per<SI::mmap>,            sc::per<SI::futex>>);
static_assert(!std::is_same_v<sc::per<SI::futex>,           sc::per<SI::socket>>);
static_assert(!std::is_same_v<sc::per<SI::socket>,          sc::per<SI::clone>>);
static_assert(!std::is_same_v<sc::per<SI::clone>,           sc::per<SI::ptrace>>);
// Same-family pair — distinct Ids on the same tier must still be
// distinct types (so a binding can engage at most ONE specific syscall
// per binding without collapsing).
static_assert(!std::is_same_v<sc::per<SI::write>,           sc::per<SI::pwrite>>);
static_assert(!std::is_same_v<sc::per<SI::clock_gettime>,   sc::per<SI::clock_getres>>);

// ── Layer 6: cross-axis distinctness — per<> ≠ family_* ─────────────
// per<SyscallId::Id> and family_<tier> both route to SyscallSurface but
// are STRUCTURALLY distinct types so the duplicate-engagement gate
// rejects the (per + family_*) combination via FixyDuplicate_SyscallSurface
// rather than silent collapse.
static_assert(!std::is_same_v<sc::per<SI::write>,           sc::family_file_mutation>);
static_assert(!std::is_same_v<sc::per<SI::mmap>,            sc::family_memory_mapping>);
static_assert(!std::is_same_v<sc::per<SI::futex>,           sc::family_thread_sync>);
static_assert(!std::is_same_v<sc::per<SI::clock_gettime>,   sc::family_vdso_only>);

// ── Layer 7: family_of() coverage — exhaustive sentinel check ───────
// The constexpr classifier never falls through to the Privilege
// fallback for any shipped enumerator.  We witness via an explicit
// covering predicate; combined with the cardinality assertion below,
// this proves family_of() is total on the SyscallId domain.
[[nodiscard]] consteval bool every_syscall_id_classified_correctly() noexcept {
    // VdsoOnly tier — 4 enumerators
    if (sc::family_of(SI::clock_gettime)     != SF::VdsoOnly)       return false;
    if (sc::family_of(SI::clock_getres)      != SF::VdsoOnly)       return false;
    if (sc::family_of(SI::getcpu_vdso)       != SF::VdsoOnly)       return false;
    if (sc::family_of(SI::gettimeofday)      != SF::VdsoOnly)       return false;
    // ReadOnlyState tier — 8 enumerators
    if (sc::family_of(SI::getpid)            != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::getppid)           != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::getuid)            != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::geteuid)           != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::getgid)            != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::gettid)            != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::uname)             != SF::ReadOnlyState)  return false;
    if (sc::family_of(SI::sysinfo)           != SF::ReadOnlyState)  return false;
    // FileMutation tier — 9 enumerators
    if (sc::family_of(SI::open)              != SF::FileMutation)   return false;
    if (sc::family_of(SI::openat)            != SF::FileMutation)   return false;
    if (sc::family_of(SI::close)             != SF::FileMutation)   return false;
    if (sc::family_of(SI::read)              != SF::FileMutation)   return false;
    if (sc::family_of(SI::write)             != SF::FileMutation)   return false;
    if (sc::family_of(SI::pread)             != SF::FileMutation)   return false;
    if (sc::family_of(SI::pwrite)            != SF::FileMutation)   return false;
    if (sc::family_of(SI::fsync)             != SF::FileMutation)   return false;
    if (sc::family_of(SI::fdatasync)         != SF::FileMutation)   return false;
    // MemoryMapping tier — 4 enumerators
    if (sc::family_of(SI::mmap)              != SF::MemoryMapping)  return false;
    if (sc::family_of(SI::munmap)            != SF::MemoryMapping)  return false;
    if (sc::family_of(SI::mprotect)          != SF::MemoryMapping)  return false;
    if (sc::family_of(SI::madvise)           != SF::MemoryMapping)  return false;
    // ThreadSync tier — 4 enumerators (V-180 added sched_setattr)
    if (sc::family_of(SI::futex)             != SF::ThreadSync)     return false;
    if (sc::family_of(SI::sched_yield)       != SF::ThreadSync)     return false;
    if (sc::family_of(SI::sched_setaffinity) != SF::ThreadSync)     return false;
    if (sc::family_of(SI::sched_setattr)     != SF::ThreadSync)     return false;
    // NetworkIo tier — 4 enumerators
    if (sc::family_of(SI::socket)            != SF::NetworkIo)      return false;
    if (sc::family_of(SI::connect)           != SF::NetworkIo)      return false;
    if (sc::family_of(SI::sendmsg)           != SF::NetworkIo)      return false;
    if (sc::family_of(SI::recvmsg)           != SF::NetworkIo)      return false;
    // ProcessControl tier — 2 enumerators
    if (sc::family_of(SI::clone)             != SF::ProcessControl) return false;
    if (sc::family_of(SI::execve)            != SF::ProcessControl) return false;
    // Privilege tier — 3 enumerators (V-180 added prctl)
    if (sc::family_of(SI::ptrace)            != SF::Privilege)      return false;
    if (sc::family_of(SI::capset)            != SF::Privilege)      return false;
    if (sc::family_of(SI::prctl)             != SF::Privilege)      return false;
    // V-180 MemoryMapping additions — mem-locking syscalls
    if (sc::family_of(SI::mlock2)            != SF::MemoryMapping)  return false;
    if (sc::family_of(SI::mlock)             != SF::MemoryMapping)  return false;
    if (sc::family_of(SI::munlock)           != SF::MemoryMapping)  return false;
    return true;
}
static_assert(every_syscall_id_classified_correctly(),
    "FIXY-V-098: family_of(SyscallId) classifier disagrees with the "
    "doc-block tier assignment for at least one enumerator.  Add the "
    "matching `case` arm or the classifier silently returns the "
    "Privilege fallback (top-of-chain — over-restrictive but never "
    "under-restrictive).");

// ── Cardinality pin — SyscallId catalog size ────────────────────────
// V-098 shipped 36 enumerators (4 + 8 + 9 + 4 + 3 + 4 + 2 + 2).  V-180
// appended 5 more (sched_setattr / mlock2 / mlock / munlock / prctl) at
// ordinals 36..40 to cover the warden/Hardening.h surface; new total 41
// = 4 + 8 + 9 + (4+3) + (3+1) + 4 + 2 + (2+1).  Growing the catalog
// is fine (append-only); shrinking or reordering is a federation-cache
// silent invalidation.
static constexpr std::size_t syscall_id_count =
    std::meta::enumerators_of(^^SI).size();
static_assert(syscall_id_count == 41,
    "FIXY-V-180: SyscallId catalog drifted from the 41-enumerator "
    "shipped surface (V-098's 36 + V-180's 5 warden additions).  If "
    "you're adding a new syscall, append it at the next free ordinal "
    "AND extend the family_of() switch arm AND add an arm to "
    "every_syscall_id_classified_correctly().  Reordering / shrinking "
    "the enum silently invalidates every stored row_hash (federation "
    "cache key).");

// Cross-check against V-097's lattice cardinality (9 tiers — Family.h's
// 9 tags + Per.h's 36 SyscallIds collectively cover the same axis).
static_assert(
    ::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
    "FIXY-V-098: Per.h depends on V-097's 9-tier chain.  If the lattice "
    "grew an enumerator, family_of() above is incomplete and some "
    "SyscallId would silently fall through to the Privilege fallback.");

}  // namespace detail::syscall_per_grant_self_test

}  // namespace crucible::fixy::grant
