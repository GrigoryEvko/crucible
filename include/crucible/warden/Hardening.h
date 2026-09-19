#pragma once

// Applying a policy never fails. A knob whose capability is missing is
// logged and skipped, and the returned handle reports what actually
// took effect. Destroying the handle undoes exactly those changes and
// nothing else.

#include "Policy.h"
#include "Registry.h"
#include "CpuTopology.h"

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
// Kernel ABI constants, spelled out because the toolchain headers often
// lag the running kernel. Each value is fixed by the upstream UAPI
// header named beside it, and the kernel version says what a build
// against older headers is gaining.
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25  // Linux 6.1, include/uapi/asm-generic/mman-common.h
#endif
#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 0x01  // Linux 4.4, include/uapi/asm-generic/mman.h
#endif
#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6  // Linux 3.14, include/uapi/linux/sched.h
#endif
#ifndef PR_SET_THP_DISABLE
#define PR_SET_THP_DISABLE 41  // Linux 3.15, include/uapi/linux/prctl.h
#endif
#endif

namespace crucible::warden {

// glibc declares neither this structure nor the three system calls
// below, so both the layout and the call wrappers are written out to
// match the kernel ABI.

#ifdef __linux__

struct sched_attr_t {
    uint32_t size = 0;
    uint32_t sched_policy = 0;
    uint64_t sched_flags = 0;
    int32_t sched_nice = 0;
    uint32_t sched_priority = 0;
    uint64_t sched_runtime = 0;
    uint64_t sched_deadline = 0;
    uint64_t sched_period = 0;
};

[[nodiscard, gnu::always_inline]] inline int sched_setattr_sys(pid_t pid, const sched_attr_t* attr,
                                                               unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_sched_setattr, pid, attr, flags));
}
[[nodiscard, gnu::always_inline]] inline int sched_getattr_sys(pid_t pid, sched_attr_t* attr, unsigned size,
                                                               unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_sched_getattr, pid, attr, size, flags));
}

[[nodiscard, gnu::always_inline]] inline int mlock2_sys(const void* addr, size_t len, unsigned flags) noexcept {
    return static_cast<int>(::syscall(SYS_mlock2, addr, len, flags));
}

#endif  // __linux__

namespace detail {

[[nodiscard]] inline bool warden_quiet() noexcept {
    const char* v = std::getenv("CRUCIBLE_WARDEN_QUIET");
    return v != nullptr && v[0] == '1';
}

// Log scanners match on the "[warden] " prefix and the "unavailable"
// infix, so the format is fixed:
//   "[warden] <mechanism> unavailable[: <errno-string>]\n"
[[gnu::cold]] inline void warn(const char* mechanism, int err) noexcept {
    if (warden_quiet()) return;
    if (err != 0) {
        std::fprintf(stderr, "[warden] %s unavailable: %s\n", mechanism, std::strerror(err));
    } else {
        std::fprintf(stderr, "[warden] %s unavailable\n", mechanism);
    }
}

}  // namespace detail

class AppliedPolicy {
public:
    struct LockedRegion {
        void* addr;
        size_t len;
    };

    AppliedPolicy() noexcept = default;

    AppliedPolicy(const AppliedPolicy&) = delete("AppliedPolicy owns prior-state memory; copying would revert twice");
    AppliedPolicy& operator=(const AppliedPolicy&) = delete("same reason");
    AppliedPolicy(AppliedPolicy&& o) noexcept { swap_(o); }
    AppliedPolicy& operator=(AppliedPolicy&& o) noexcept {
        if (this != &o) {
            revert();
            swap_(o);
        }
        return *this;
    }

    ~AppliedPolicy() noexcept { revert(); }

    // Idempotent, and safe to call before destruction.
    //
    // Each tracking flag clears immediately after its own syscall
    // rather than relying on the guard at the top. Move-assignment
    // reverts and then swaps, so a flag left set here would travel
    // into the moved-from handle and make it claim work it has already
    // undone.
    void revert() noexcept {
#ifdef __linux__
        if (reverted_) return;
        reverted_ = true;

        if (prior_sched_set_) {
            (void)sched_setattr_sys(0, &prior_sched_, 0);
            prior_sched_set_ = false;
        }
        if (prior_affinity_set_) {
            (void)::sched_setaffinity(0, sizeof(prior_affinity_), &prior_affinity_);
            prior_affinity_set_ = false;
        }
        for (const auto& r : locked_) {
            (void)::munlock(r.addr, r.len);
        }
        locked_.clear();
        if (thp_globally_disabled_) {
            (void)::prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
            thp_globally_disabled_ = false;
        }
        // Restoring the prior affinity mask retracts the claim that
        // this thread sits on one named CPU.
        pinned_cpu_ = -1;
#endif
    }

    [[nodiscard]] bool scheduler_applied() const noexcept { return prior_sched_set_; }
    [[nodiscard]] bool affinity_applied() const noexcept { return prior_affinity_set_; }
    [[nodiscard]] size_t regions_locked() const noexcept { return locked_.size(); }
    [[nodiscard]] int pinned_cpu() const noexcept { return pinned_cpu_; }

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

    bool reverted_ = false;
#ifdef __linux__
    sched_attr_t prior_sched_{};
    cpu_set_t prior_affinity_{};
#else
    int prior_sched_ = 0;
    int prior_affinity_ = 0;
#endif
    bool prior_sched_set_ = false;
    bool prior_affinity_set_ = false;
    bool thp_globally_disabled_ = false;
    int pinned_cpu_ = -1;
    std::vector<LockedRegion> locked_{};
};

class Hardening {
public:
    // Hold the returned guard for as long as the policy is to stay in
    // effect. Setting CRUCIBLE_WARDEN_QUIET to 1 suppresses the
    // warnings that a skipped knob emits.
    [[nodiscard]] static AppliedPolicy apply(const Policy& p) noexcept {
        AppliedPolicy g;
        if (!p.hot_enabled) return g;

#ifdef __linux__
        {
            const int cpu = select_hot_cpu(p.hot_core);
            if (cpu >= 0) {
                cpu_set_t prior;
                CPU_ZERO(&prior);
                if (::sched_getaffinity(0, sizeof(prior), &prior) == 0) {
                    g.prior_affinity_ = prior;
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

        // A real-time class on a shared core starves everything else
        // that runs there, so it is skipped unless the core is
        // isolated. Setting CRUCIBLE_WARDEN_FORCE to 1 overrides this.
        bool realtime_allowed = true;
        if (p.hot_sched != SchedClass::Other && g.pinned_cpu_ >= 0) {
            const auto iso = isolated_cpus();
            const bool on_isolcpu = std::find(iso.begin(), iso.end(), g.pinned_cpu_) != iso.end();
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
                g.prior_sched_ = prior;
                g.prior_sched_set_ = true;
            }
            sched_attr_t attr{};
            attr.size = sizeof(attr);
            switch (p.hot_sched) {
                case SchedClass::Fifo:
                    attr.sched_policy = SCHED_FIFO;
                    attr.sched_priority = static_cast<uint32_t>(p.hot_rt_priority);
                    break;
                case SchedClass::RoundRobin:
                    attr.sched_policy = SCHED_RR;
                    attr.sched_priority = static_cast<uint32_t>(p.hot_rt_priority);
                    break;
                case SchedClass::Batch:
                    attr.sched_policy = SCHED_BATCH;
                    break;
                case SchedClass::Idle:
                    attr.sched_policy = SCHED_IDLE;
                    break;
                case SchedClass::Deadline:
                    attr.sched_policy = SCHED_DEADLINE;
                    attr.sched_runtime = p.hot_runtime_ns;
                    attr.sched_deadline = p.hot_deadline_ns;
                    attr.sched_period = p.hot_period_ns;
                    break;
                case SchedClass::Other:
                default:
                    attr.sched_policy = SCHED_OTHER;
                    break;
            }
            if (sched_setattr_sys(0, &attr, 0) != 0) {
                const int err = errno;
                // From Linux 5.8 the deadline class refuses a thread
                // whose affinity is a strict subset of the deadline
                // root domain, which is exactly the pinning done
                // above. Fall back to the first-in-first-out class.
                if (p.hot_sched == SchedClass::Deadline && err == EPERM) {
                    sched_attr_t fifo{};
                    fifo.size = sizeof(fifo);
                    fifo.sched_policy = SCHED_FIFO;
                    fifo.sched_priority = static_cast<uint32_t>(p.hot_rt_priority);
                    if (sched_setattr_sys(0, &fifo, 0) == 0) {
                        std::fprintf(stderr,
                                     "[warden] SCHED_DEADLINE EPERM — fell back to "
                                     "SCHED_FIFO prio=%d\n",
                                     p.hot_rt_priority);
                    } else {
                        g.prior_sched_set_ = false;
                        detail::warn("sched_setattr (FIFO fallback)", errno);
                    }
                } else {
                    g.prior_sched_set_ = false;
                    detail::warn("sched_setattr (real-time class)", err);
                }
            }
        }

        // This knob is process-wide, unlike the thread-scoped ones
        // above, and children inherit it.
        if (p.disable_thp_global) {
            if (::prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0) == 0) {
                g.thp_globally_disabled_ = true;
            } else {
                detail::warn("prctl(PR_SET_THP_DISABLE)", errno);
            }
        }

        // The three steps run in this order for each region. The huge
        // page hint has to be in place before the first fault, and a
        // collapse only succeeds once the pages are present.
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
        (void)g;
        (void)p;
#endif

        return g;
    }

    [[nodiscard]] static bool lock_region(AppliedPolicy& g, void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;
        // MLOCK_ONFAULT locks each page as it faults in rather than
        // faulting the whole region at once.
        if (mlock2_sys(addr, len, MLOCK_ONFAULT) == 0) {
            g.locked_.push_back({addr, len});
            return true;
        }
        // A kernel without mlock2 takes the eager call instead.
        if (::mlock(addr, len) == 0) {
            g.locked_.push_back({addr, len});
            return true;
        }
        detail::warn("mlock2/mlock", errno);
        return false;
#else
        (void)g;
        (void)addr;
        (void)len;
        return false;
#endif
    }

    // From Linux 5.8 the advice call rejects an address that is not
    // aligned to a huge page, so the range is rounded inward first.
    [[nodiscard]] static bool hint_hugepage(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;

        const uintptr_t raw = std::bit_cast<uintptr_t>(addr);
        const uintptr_t aligned = (raw + kHugePageBytes - 1) & ~(kHugePageBytes - 1);
        const size_t lost_head = aligned - raw;
        if (lost_head >= len) return false;
        const size_t usable = len - lost_head;
        const size_t aligned_len = usable & ~(kHugePageBytes - 1);
        if (aligned_len == 0) return false;

        void* const target = std::bit_cast<void*>(aligned);
        if (::madvise(target, aligned_len, MADV_HUGEPAGE) == 0) return true;
        detail::warn("madvise(MADV_HUGEPAGE)", errno);
        return false;
#else
        (void)addr;
        (void)len;
        return false;
#endif
    }

    // Linux 6.1 and later. The collapse happens before the call
    // returns, and the kernel emits a tracepoint an observer can count.
    [[nodiscard]] static bool collapse_hugepage(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;

        const uintptr_t raw = std::bit_cast<uintptr_t>(addr);
        const uintptr_t aligned = (raw + kHugePageBytes - 1) & ~(kHugePageBytes - 1);
        const size_t lost_head = aligned - raw;
        if (lost_head >= len) return false;
        const size_t usable = len - lost_head;
        const size_t aligned_len = usable & ~(kHugePageBytes - 1);
        if (aligned_len == 0) return false;

        void* const target = std::bit_cast<void*>(aligned);
        if (::madvise(target, aligned_len, MADV_COLLAPSE) == 0) return true;
        // A kernel that does not know the advice reports ENOSYS or
        // EINVAL. That is expected and stays silent.
        if (errno != ENOSYS && errno != EINVAL) {
            detail::warn("madvise(MADV_COLLAPSE)", errno);
        }
        return false;
#else
        (void)addr;
        (void)len;
        return false;
#endif
    }

    // Touching one byte per page populates the page tables up front.
    // The read-write through a volatile pointer is what stops the
    // compiler from discarding the touch. There is nothing to revert.
    [[gnu::cold]] static void prefault(void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return;
        const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
        volatile unsigned char* p = static_cast<unsigned char*>(addr);
        for (size_t off = 0; off < len; off += page)
            p[off] = p[off];
#else
        (void)addr;
        (void)len;
#endif
    }
};

[[nodiscard]] inline AppliedPolicy apply(const Policy& p) noexcept { return Hardening::apply(p); }

// Applying a policy mutates process-wide state through privileged
// system calls, which belongs to start-up only. A hot foreground or
// background context must not reach this surface.
template <class Ctx>
concept CtxFitsHardeningMint = effects::IsExecCtx<Ctx> && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

// The privileged system calls this surface issues, named once at the
// type level so the set is discoverable and auditable. This is a
// classification, not a gate: admission is decided by the concept
// above, which requires only the start-up capability.
//
// The set covers reads as well as writes. An auditor asking what this
// code does to the kernel is owed the whole answer, and apply() reads
// the prior affinity mask and the prior scheduling attributes before it
// replaces them. Leaving the two read halves out made the declared set
// smaller than the truth while every check beside it stayed green.
//
// scripts/check-syscall-grant-coverage.sh derives this set from the
// call sites in this header and fails when the two disagree, so the
// list is checked against the code rather than against its own prose.
using mint_hardening_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::sched_setaffinity>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::sched_setattr>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mlock>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mlock2>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::munlock>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::madvise>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::prctl>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::sched_getaffinity>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::sched_getattr>>;

// Each grant's declared family is checked against the classifier here,
// so a disagreement fails while this header is parsed rather than at
// some later call site.
namespace detail::hardening_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;

static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::sched_setaffinity>>
              == fll::SyscallFamily::ThreadSync);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::sched_setattr>>
              == fll::SyscallFamily::ThreadSync);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mlock>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mlock2>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::munlock>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::madvise>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::prctl>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::sched_getaffinity>>
              == fll::SyscallFamily::ThreadSync);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::sched_getattr>>
              == fll::SyscallFamily::ThreadSync);

static_assert(std::tuple_size_v<mint_hardening_syscall_grants> == 9,
              "mint_hardening_syscall_grants no longer holds 9 entries. A syscall "
              "added to Hardening::apply() needs an entry in the tuple and a family "
              "check beside it. A syscall removed from the set changes the cache key "
              "derived from it, so audit the removal first. This count agrees with "
              "the code only because scripts/check-syscall-grant-coverage.sh derives "
              "the set from the call sites; a hand-written count is a claim about "
              "the code that nothing reads the code to confirm.");
}  // namespace detail::hardening_grant_check

template <effects::IsExecCtx Ctx>
    requires CtxFitsHardeningMint<Ctx>
[[nodiscard]] inline AppliedPolicy mint_hardening(Ctx const&, const Policy& policy) noexcept {
    return Hardening::apply(policy);
}

static_assert(CtxFitsHardeningMint<effects::ColdInitCtx>);
static_assert(!CtxFitsHardeningMint<effects::BgDrainCtx>);
static_assert(!CtxFitsHardeningMint<effects::HotFgCtx>);

}  // namespace crucible::warden
