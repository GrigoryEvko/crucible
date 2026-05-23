#pragma once

// Apply a `crucible::warden::Policy` to the current thread / process.
//
// Every knob has a graceful-degradation path: if a capability is
// missing, we log (unless CRUCIBLE_WARDEN_QUIET=1) and continue. The
// returned `AppliedPolicy` is an RAII handle that remembers what
// actually changed and reverts it on destruction — critical for the
// bench harness, which applies a Policy per-Run and then expects the
// process to return to dev defaults.
//
// Usage:
//     auto applied = crucible::warden::apply(Policy::dev_quiet());
//     // ... work ...
//     // `applied` destructs → prior scheduler/affinity/locks restored
//
// For the Keeper, the guard is held until shutdown. For bench::Run,
// it's stack-scoped to measure().
//
// Every syscall is inlined; this header is the entire implementation.
// Linux x86_64 only for now; the file short-circuits on other
// platforms to an empty AppliedPolicy (policy has no effect).

#include "Policy.h"
#include "Registry.h"
#include "CpuTopology.h"

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>            // FIXY-V-180: SyscallId + per<Id>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>  // FIXY-V-180: family_tier check

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>           // FIXY-V-180: mint_hardening_syscall_grants
#include <utility>
#include <vector>

#ifdef __linux__
#  include <sched.h>
#  include <sys/mman.h>
#  include <sys/prctl.h>
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <unistd.h>
// Kernel-ABI constants. Defined here when toolchain headers lag behind the
// running kernel (common in glibc packaging). Values come from the upstream
// Linux UAPI headers and are stable. Comments cite the kernel version that
// introduced each constant so it's clear what we're feature-gating against.
#  ifndef MADV_COLLAPSE
#    define MADV_COLLAPSE 25          // Linux 6.1 (2022-12). include/uapi/asm-generic/mman-common.h
#  endif
#  ifndef MLOCK_ONFAULT
#    define MLOCK_ONFAULT 0x01        // Linux 4.4 (2016-01). include/uapi/asm-generic/mman.h
#  endif
#  ifndef SCHED_DEADLINE
#    define SCHED_DEADLINE 6          // Linux 3.14 (2014-03). include/uapi/linux/sched.h
#  endif
#  ifndef PR_SET_THP_DISABLE
#    define PR_SET_THP_DISABLE 41     // Linux 3.15 (2014-06). include/uapi/linux/prctl.h
#  endif
#endif

namespace crucible::warden {

// ── Linux struct sched_attr (not in glibc) ─────────────────────────

#ifdef __linux__

struct sched_attr_t {
    uint32_t size            = 0;
    uint32_t sched_policy    = 0;
    uint64_t sched_flags     = 0;
    int32_t  sched_nice      = 0;
    uint32_t sched_priority  = 0;
    uint64_t sched_runtime   = 0;
    uint64_t sched_deadline  = 0;
    uint64_t sched_period    = 0;
};

[[nodiscard, gnu::always_inline]] inline int
sched_setattr_sys(pid_t pid, const sched_attr_t* attr, unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_sched_setattr, pid, attr, flags));
}
[[nodiscard, gnu::always_inline]] inline int
sched_getattr_sys(pid_t pid, sched_attr_t* attr, unsigned size, unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_sched_getattr, pid, attr, size, flags));
}

[[nodiscard, gnu::always_inline]] inline int
mlock2_sys(const void* addr, size_t len, unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_mlock2, addr, len, flags));
}

#endif // __linux__

// ── Diagnostics ────────────────────────────────────────────────────

namespace detail {

[[nodiscard]] inline bool warden_quiet() noexcept {
    const char* v = std::getenv("CRUCIBLE_WARDEN_QUIET");
    return v != nullptr && v[0] == '1';
}

// Cold-path diagnostic. Only fires when a capability is missing. The
// "[warden] " prefix plus "unavailable:" infix is the stable grep anchor for
// automated log scanners (bench harness, Keeper health monitors, CI).
// Format: "[warden] <mechanism> unavailable[: <errno-string>]\n"
[[gnu::cold]] inline void warn(const char* mechanism, int err) noexcept {
    if (warden_quiet()) return;
    if (err != 0) {
        std::fprintf(stderr, "[warden] %s unavailable: %s\n", mechanism, std::strerror(err));
    } else {
        std::fprintf(stderr, "[warden] %s unavailable\n", mechanism);
    }
}

} // namespace detail

// ── AppliedPolicy: RAII revert-on-destroy ──────────────────────────

class AppliedPolicy {
 public:
    struct LockedRegion { void* addr; size_t len; };

    AppliedPolicy() noexcept = default;

    // Non-copyable, movable.
    AppliedPolicy(const AppliedPolicy&) = delete("AppliedPolicy owns prior-state memory; copying would revert twice");
    AppliedPolicy& operator=(const AppliedPolicy&) = delete("same reason");
    AppliedPolicy(AppliedPolicy&& o) noexcept { swap_(o); }
    AppliedPolicy& operator=(AppliedPolicy&& o) noexcept { if (this != &o) { revert(); swap_(o); } return *this; }

    ~AppliedPolicy() noexcept { revert(); }

    // Manual early-revert. Idempotent.
    //
    // fixy-A5-017: each tracking flag is cleared IMMEDIATELY after its
    // syscall runs, not just at the top via `reverted_=true`.  The old
    // code left `prior_sched_set_` / `prior_affinity_set_` /
    // `thp_globally_disabled_` / `pinned_cpu_` set after revert; the
    // operator=(&&) `revert(); swap_(o);` dance then propagated those
    // stale flags into the moved-from object — so the source reported
    // `scheduler_applied()==true` AND `affinity_applied()==true` on a
    // handle that had already undone its work.  The `reverted_` guard
    // at the top prevents LITERAL double-revert, but observers lied
    // about the post-revert state.  Self-clearing here means the
    // post-swap moved-from object is fully disarmed: every observer
    // returns the disarmed answer.
    void revert() noexcept {
#ifdef __linux__
        if (reverted_) return;
        reverted_ = true;

        // Undo sched policy / nice in reverse order.
        if (prior_sched_set_) {
            (void)sched_setattr_sys(0, &prior_sched_, 0);
            prior_sched_set_ = false;
        }
        // sched_setaffinity restore (only if we changed it).
        if (prior_affinity_set_) {
            (void)::sched_setaffinity(0, sizeof(prior_affinity_), &prior_affinity_);
            prior_affinity_set_ = false;
        }
        // Unlock any regions we locked.
        for (const auto& r : locked_) {
            (void)::munlock(r.addr, r.len);
        }
        locked_.clear();
        // Re-enable THP if we turned it off.
        if (thp_globally_disabled_) {
            (void)::prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
            thp_globally_disabled_ = false;
        }
        // pinned_cpu_ is a derived observable — once affinity is restored
        // the "we pinned to CPU N" assertion no longer holds.
        pinned_cpu_ = -1;
#endif
    }

    // Observers — what actually took effect.
    [[nodiscard]] bool scheduler_applied()   const noexcept { return prior_sched_set_; }
    [[nodiscard]] bool affinity_applied()    const noexcept { return prior_affinity_set_; }
    [[nodiscard]] size_t regions_locked()    const noexcept { return locked_.size(); }
    [[nodiscard]] int pinned_cpu()           const noexcept { return pinned_cpu_; }

 private:
    friend class Hardening;
    void swap_(AppliedPolicy& o) noexcept {
        std::swap(reverted_, o.reverted_);
        std::swap(prior_sched_, o.prior_sched_);
        std::swap(prior_sched_set_, o.prior_sched_set_);
#ifdef __linux__
        std::swap(prior_affinity_, o.prior_affinity_);
#endif
        std::swap(prior_affinity_set_, o.prior_affinity_set_);
        std::swap(thp_globally_disabled_, o.thp_globally_disabled_);
        std::swap(pinned_cpu_, o.pinned_cpu_);
        locked_.swap(o.locked_);
    }

    bool         reverted_              = false;
#ifdef __linux__
    sched_attr_t prior_sched_{};
    cpu_set_t    prior_affinity_{};
#else
    int          prior_sched_ = 0;
    int          prior_affinity_ = 0;
#endif
    bool         prior_sched_set_       = false;
    bool         prior_affinity_set_    = false;
    bool         thp_globally_disabled_ = false;  // we disabled THP and need to re-enable
    int          pinned_cpu_            = -1;
    std::vector<LockedRegion> locked_{};
};

// ── Hardening: the apply() function + helpers ─────────────────────

class Hardening {
 public:
    // Apply the policy to the calling thread / process. Returns an
    // RAII guard; hold it as long as the policy should be in effect.
    // On failure of any single mechanism, logs a warning (unless
    // CRUCIBLE_WARDEN_QUIET=1) and continues — the returned guard
    // reflects what actually took effect.
    [[nodiscard]] static AppliedPolicy apply(const Policy& p) noexcept {
        AppliedPolicy g;
        if (!p.hot_enabled) return g;

#ifdef __linux__
        // 1. Pin the current thread to the selected HOT core.
        {
            const int cpu = select_hot_cpu(p.hot_core);
            if (cpu >= 0) {
                cpu_set_t prior;
                CPU_ZERO(&prior);
                if (::sched_getaffinity(0, sizeof(prior), &prior) == 0) {
                    g.prior_affinity_     = prior;
                    g.prior_affinity_set_ = true;

                    cpu_set_t set;
                    CPU_ZERO(&set);
                    CPU_SET(static_cast<size_t>(cpu), &set);
                    if (::sched_setaffinity(0, sizeof(set), &set) == 0) {
                        g.pinned_cpu_ = cpu;
                    } else {
                        g.prior_affinity_set_ = false;
                        detail::warn("sched_setaffinity", errno);
                    }
                } else {
                    detail::warn("sched_getaffinity", errno);
                }
            }
        }

        // 2. Scheduler policy. Skip RT on a non-isolated CPU — FIFO/RR
        // on a shared core starves whatever else lives there (Wayland
        // compositor → frozen cursor). Override with CRUCIBLE_WARDEN_FORCE=1.
        bool realtime_allowed = true;
        if (p.hot_sched != SchedClass::Other && g.pinned_cpu_ >= 0) {
            const auto iso = isolated_cpus();
            const bool on_isolcpu = std::find(iso.begin(), iso.end(),
                                              g.pinned_cpu_) != iso.end();
            if (!on_isolcpu) {
                const char* force = std::getenv("CRUCIBLE_WARDEN_FORCE");
                if (!force || std::strcmp(force, "1") != 0) {
                    std::fprintf(stderr,
                        "[warden] CPU %d not isolated — skipping RT class "
                        "(set CRUCIBLE_WARDEN_FORCE=1 or boot isolcpus=%d).\n",
                        g.pinned_cpu_, g.pinned_cpu_);
                    realtime_allowed = false;
                }
            }
        }

        if (p.hot_sched != SchedClass::Other && realtime_allowed) {
            sched_attr_t prior{};
            if (sched_getattr_sys(0, &prior, sizeof(prior), 0) == 0) {
                g.prior_sched_     = prior;
                g.prior_sched_set_ = true;
            }
            sched_attr_t attr{};
            attr.size = sizeof(attr);
            switch (p.hot_sched) {
            case SchedClass::Fifo:
                attr.sched_policy   = SCHED_FIFO;
                attr.sched_priority = static_cast<uint32_t>(p.hot_rt_priority);
                break;
            case SchedClass::RoundRobin:
                attr.sched_policy   = SCHED_RR;
                attr.sched_priority = static_cast<uint32_t>(p.hot_rt_priority);
                break;
            case SchedClass::Batch:
                attr.sched_policy = SCHED_BATCH;
                break;
            case SchedClass::Idle:
                attr.sched_policy = SCHED_IDLE;
                break;
            case SchedClass::Deadline:
                attr.sched_policy   = SCHED_DEADLINE;
                attr.sched_runtime  = p.hot_runtime_ns;
                attr.sched_deadline = p.hot_deadline_ns;
                attr.sched_period   = p.hot_period_ns;
                break;
            case SchedClass::Other:
            default:
                attr.sched_policy = SCHED_OTHER;
                break;
            }
            if (sched_setattr_sys(0, &attr, 0) != 0) {
                const int err = errno;
                // DEADLINE EPERMs when affinity is a strict subset of the
                // DL root domain (kernel 5.8+); fall back to FIFO.
                if (p.hot_sched == SchedClass::Deadline && err == EPERM) {
                    sched_attr_t fifo{};
                    fifo.size           = sizeof(fifo);
                    fifo.sched_policy   = SCHED_FIFO;
                    fifo.sched_priority =
                        static_cast<uint32_t>(p.hot_rt_priority);
                    if (sched_setattr_sys(0, &fifo, 0) == 0) {
                        std::fprintf(stderr,
                            "[warden] SCHED_DEADLINE EPERM — fell back to "
                            "SCHED_FIFO prio=%d\n", p.hot_rt_priority);
                    } else {
                        g.prior_sched_set_ = false;
                        detail::warn("sched_setattr (FIFO fallback)",
                                     errno);
                    }
                } else {
                    g.prior_sched_set_ = false;
                    detail::warn("sched_setattr (real-time class)", err);
                }
            }
        }

        // 3. THP global disable (process-wide).
        if (p.disable_thp_global) {
            if (::prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0) == 0) {
                g.thp_globally_disabled_ = true;
            } else {
                detail::warn("prctl(PR_SET_THP_DISABLE)", errno);
            }
        }

        // 4. Per registered region: HUGEPAGE hint, mlock, then COLLAPSE.
        // Order matters: the hint must be set before any fault, and
        // COLLAPSE only makes `thp_ok` tick after pages are present.
        if (p.mlock_hot_regions || p.thp_hint_pools || p.thp_collapse_now) {
            const auto regions = HotRegionRegistry::instance().snapshot();
            for (const auto& r : regions) {
                if (p.thp_hint_pools && r.huge_hint) {
                    (void)hint_hugepage(r.addr, r.len);
                }
                if (p.mlock_hot_regions) {
                    (void)lock_region(g, r.addr, r.len);
                }
                if (p.thp_collapse_now && r.huge_hint) {
                    (void)collapse_hugepage(r.addr, r.len);
                }
            }
        }
#else
        (void)g; (void)p;
#endif

        return g;
    }

    // Register a memory region to be locked under the current (or a
    // newly-applied) policy. Called from PoolAllocator, MemoryPlan,
    // TraceRing, KernelCache init paths.
    //
    // Returns true on success. Stores the (addr, len) in the provided
    // guard so it gets unlocked on revert.
    [[nodiscard]] static bool lock_region(AppliedPolicy& g, void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;
        // MLOCK_ONFAULT → lock pages as they fault in, not eagerly.
        if (mlock2_sys(addr, len, MLOCK_ONFAULT) == 0) {
            g.locked_.push_back({addr, len});
            return true;
        }
        // Fall back to eager mlock for older kernels.
        if (::mlock(addr, len) == 0) {
            g.locked_.push_back({addr, len});
            return true;
        }
        detail::warn("mlock2/mlock", errno);
        return false;
#else
        (void)g; (void)addr; (void)len;
        return false;
#endif
    }

    // MADV_HUGEPAGE with defense-in-depth PMD rounding (kernel 5.8+
    // EINVALs non-aligned addresses).
    [[nodiscard]] static bool hint_hugepage(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;

        const uintptr_t raw       = std::bit_cast<uintptr_t>(addr);
        const uintptr_t aligned   = (raw + kHugePageBytes - 1)
                                  & ~(kHugePageBytes - 1);
        const size_t    lost_head = aligned - raw;
        if (lost_head >= len) return false;
        const size_t usable      = len - lost_head;
        const size_t aligned_len = usable & ~(kHugePageBytes - 1);
        if (aligned_len == 0) return false;

        void* const target = std::bit_cast<void*>(aligned);
        if (::madvise(target, aligned_len, MADV_HUGEPAGE) == 0) return true;
        detail::warn("madvise(MADV_HUGEPAGE)", errno);
        return false;
#else
        (void)addr; (void)len;
        return false;
#endif
    }

    // MADV_COLLAPSE (kernel ≥ 6.1) — synchronous; ticks the BPF thp_ok
    // counter via the mm_collapse_huge_page tracepoint.
    [[nodiscard]] static bool collapse_hugepage(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;

        const uintptr_t raw       = std::bit_cast<uintptr_t>(addr);
        const uintptr_t aligned   = (raw + kHugePageBytes - 1)
                                  & ~(kHugePageBytes - 1);
        const size_t    lost_head = aligned - raw;
        if (lost_head >= len) return false;
        const size_t usable      = len - lost_head;
        const size_t aligned_len = usable & ~(kHugePageBytes - 1);
        if (aligned_len == 0) return false;

        void* const target = std::bit_cast<void*>(aligned);
        if (::madvise(target, aligned_len, MADV_COLLAPSE) == 0) return true;
        // ENOSYS / EINVAL on older kernels — silent.
        if (errno != ENOSYS && errno != EINVAL) {
            detail::warn("madvise(MADV_COLLAPSE)", errno);
        }
        return false;
#else
        (void)addr; (void)len;
        return false;
#endif
    }

    // Prefault: touch one byte per page so page tables are populated.
    // Called from Meridian's calibration stage. No revert. Cold-marked
    // because each region is prefaulted exactly once at init time; the
    // hot path never visits this function.
    [[gnu::cold]] static void prefault(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return;
        const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
        volatile unsigned char* p = static_cast<unsigned char*>(addr);
        for (size_t off = 0; off < len; off += page) p[off] = p[off];
#else
        (void)addr; (void)len;
#endif
    }
};

// Free-function shorthand matching the CRUCIBLE.md §16 spec.
[[nodiscard]] inline AppliedPolicy apply(const Policy& p) noexcept {
    return Hardening::apply(p);
}

// ── §XXI Universal Mint Pattern — mint_hardening (FIXY-U-084) ─────────
//
// CtxFitsHardeningMint admits only contexts whose effect row carries the
// Init capability.  Hardening::apply() performs Linux syscalls
// (sched_setaffinity, sched_setattr, mlock2, mlock, munlock,
// madvise(MADV_HUGEPAGE/MADV_COLLAPSE), prctl(PR_SET_THP_DISABLE))
// which are process-wide state mutations belonging to the startup-only
// Init row.  Hot foreground and background-drain contexts must not
// engage this surface.
//
// The mint is the §XXI authorization point; downstream apply() callers
// can continue to use the bare free function during the migration period.
template <class Ctx>
concept CtxFitsHardeningMint =
       effects::IsExecCtx<Ctx>
    && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

// ── FIXY-V-180 — syscall-grant declaration ────────────────────────────
//
// `mint_hardening_syscall_grants` enumerates every privileged Linux
// syscall Hardening::apply() issues.  It is a type-level audit-trail:
//
//   * `grep "mint_hardening_syscall_grants"` finds the canonical
//     classification of warden's syscall surface (one site, type-level).
//   * Each grant's `family_tier_v<G>` is locked at compile time via
//     the static_assert block below — drift between the declared trait
//     and V-098's per-syscall classifier reds at this header's parse,
//     before any consumer sees the mint.
//   * Per Agent 3 Phase Σ4: "mint_hardening is THE ONLY Crucible
//     function authorized to issue these privileged syscalls" — the
//     declaration is the grep-discoverable seal on that contract.
//
// Family-tier table (V-097 SyscallFamily chain; cross-reference V-100
// Bridge.h's row-lift map):
//
//   sched_setaffinity (27) → ThreadSync      → Row<Block>
//   sched_setattr     (36) → ThreadSync      → Row<Block>          [V-180]
//   mlock             (38) → MemoryMapping   → Row<IO>             [V-180]
//   mlock2            (37) → MemoryMapping   → Row<IO>             [V-180]
//   munlock           (39) → MemoryMapping   → Row<IO>             [V-180]
//   madvise           (24) → MemoryMapping   → Row<IO>
//   prctl             (40) → Privilege       → Row<IO, Block>      [V-180]
//
// Row note: ColdInitCtx is `Row<Init, Alloc, IO>` and does NOT include
// `Block`; the runtime acceptance gate is `CtxOwnsCapability<Ctx,
// Effect::Init>` (above) — Init is the startup-time pass-through that
// admits all blocking work without requiring `Block` in the row.  The
// syscall_grants declaration below is a CLASSIFICATION ANNOTATION, not
// a row-subrow gate; future tightening lives in V-131 H003 (cross-axis
// permission<Root> proof gate).
//
// The type is a tuple — distinct types per syscall, federation-cache
// discriminable (V-098 NTTP discipline).
using mint_hardening_syscall_grants = std::tuple<
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::sched_setaffinity>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::sched_setattr>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::mlock>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::mlock2>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::munlock>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::madvise>,
    ::crucible::fixy::grant::syscall::per<
        ::crucible::fixy::grant::syscall::SyscallId::prctl>>;

// Lock the declared family_tier for every grant in the set against
// V-098's per<Id> classifier — drift here reds at parse, before any
// consumer reaches the mint.
namespace detail::v180_hardening_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;

static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::sched_setaffinity>> == fll::SyscallFamily::ThreadSync);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::sched_setattr>>    == fll::SyscallFamily::ThreadSync);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::mlock>>            == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::mlock2>>           == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::munlock>>          == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::madvise>>          == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<
    fsc::per<fsc::SyscallId::prctl>>            == fll::SyscallFamily::Privilege);

// Tuple cardinality pin — adding a syscall to the set must update both
// the using-decl above AND this sentinel.
static_assert(std::tuple_size_v<mint_hardening_syscall_grants> == 7,
    "FIXY-V-180: mint_hardening_syscall_grants drifted from 7 entries.  "
    "If you added a syscall to Hardening::apply(), append the new "
    "per<SyscallId::X> to the tuple AND extend SyscallId in "
    "fixy/syscall/Per.h (append-only) AND add a family_tier_v check "
    "below.  If you removed a syscall, the federation-cache key drifts; "
    "audit before committing.");
}  // namespace detail::v180_hardening_grant_check

template <effects::IsExecCtx Ctx>
    requires CtxFitsHardeningMint<Ctx>
[[nodiscard]] inline AppliedPolicy
mint_hardening(Ctx const&, const Policy& policy) noexcept {
    return Hardening::apply(policy);
}

static_assert(CtxFitsHardeningMint<effects::ColdInitCtx>);
static_assert(!CtxFitsHardeningMint<effects::BgDrainCtx>);
static_assert(!CtxFitsHardeningMint<effects::HotFgCtx>);

} // namespace crucible::warden
