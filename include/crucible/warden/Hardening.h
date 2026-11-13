#pragma once

// Applying a policy never fails. A knob whose capability is missing is
// logged and skipped, and the returned handle reports what actually
// took effect. Destroying the handle undoes exactly those changes and
// nothing else.

#include "Policy.h"
#include "Registry.h"
#include "CpuTopology.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/Sched.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Auxiliary.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <utility>

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

// True when the environment variable name holds the text "1".
[[nodiscard]] inline bool is_env_set_to_one(const char* name) noexcept {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

// Log scanners match on the "[warden] " prefix and the "unavailable"
// infix, so the format is fixed:
//   "[warden] <mechanism> unavailable[: error <number>]\n"
[[gnu::cold]] inline void warn(::fixy::TextView mechanism, int err) noexcept {
    if (is_env_set_to_one("CRUCIBLE_WARDEN_QUIET")) return;
    if (err != 0) {
        ::fixy::report(::fixy::Sink::Err, "[warden] {} unavailable: error {}\n", mechanism, err);
    } else {
        ::fixy::report(::fixy::Sink::Err, "[warden] {} unavailable\n", mechanism);
    }
}

}  // namespace detail

class AppliedPolicy {
public:
    struct LockedRegion {
        void* addr = nullptr;
        size_t len = 0;
    };

    AppliedPolicy() noexcept = default;

    AppliedPolicy(const AppliedPolicy&) = delete("AppliedPolicy owns prior-state memory; copying would revert twice");
    AppliedPolicy& operator=(const AppliedPolicy&) = delete("same reason");
    AppliedPolicy(AppliedPolicy&& o) noexcept { take_(o); }
    AppliedPolicy& operator=(AppliedPolicy&& o) noexcept {
        if (this != &o) {
            revert();
            take_(o);
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
    //
    // The revert is teardown work of system calls, so no caller inlines
    // it.  A caller pays one call, and a measurement loop keeps its code
    // small.
    CRUCIBLE_NOINLINE void revert() noexcept {
#ifdef __linux__
        if (reverted_) return;
        reverted_ = true;

        if (prior_sched_set_) {
            (void)sched_setattr_sys(0, &prior_sched_, 0);
            prior_sched_set_ = false;
        }
        // The restore goes through the affinity door, so a pin proof of the
        // hot CPU stops being in force.  The match leaves the Option empty.
        std::move(prior_affinity_)
            .match([](::fixy::sched::PriorAffinity prior) noexcept { (void)std::move(prior).restore(); },
                   []() noexcept {});
        for (const LockedRegion& r : locked_) {
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
    [[nodiscard]] bool affinity_applied() const noexcept { return prior_affinity_.is_some(); }
    [[nodiscard]] size_t regions_locked() const noexcept { return locked_.size(); }
    [[nodiscard]] int pinned_cpu() const noexcept { return pinned_cpu_; }

private:
    friend class Hardening;

    // Takes each change that source holds, and leaves source with no
    // change to undo.
    void take_(AppliedPolicy& source) noexcept {
        reverted_ = source.reverted_;
        prior_sched_ = source.prior_sched_;
        prior_sched_set_ = source.prior_sched_set_;
        // The match moves the mask out of source, and the assignment takes a
        // new Option that is full.  A move assignment from source itself
        // gives a false maybe-uninitialized error of GCC 16 at -O3 with ASan,
        // because the compiler does not see that the slot of source is full.
        std::move(source.prior_affinity_)
            .match(
                [this](::fixy::sched::PriorAffinity prior) noexcept {
                    prior_affinity_ = ::fixy::Option<::fixy::sched::PriorAffinity>::some(std::move(prior));
                },
                [this]() noexcept { prior_affinity_ = ::fixy::none; });
        thp_globally_disabled_ = source.thp_globally_disabled_;
        pinned_cpu_ = source.pinned_cpu_;
        locked_ = std::move(source.locked_);
        source.prior_sched_set_ = false;
        source.prior_affinity_ = ::fixy::none;
        source.thp_globally_disabled_ = false;
        source.pinned_cpu_ = -1;
        source.locked_.clear();
    }

    bool reverted_ = false;
#ifdef __linux__
    sched_attr_t prior_sched_{};
#else
    int prior_sched_ = 0;
#endif
    // The mask that the pin of apply replaced, held while the pin stays.
    ::fixy::Option<::fixy::sched::PriorAffinity> prior_affinity_{};
    bool prior_sched_set_ = false;
    bool thp_globally_disabled_ = false;
    int pinned_cpu_ = -1;
    // Apply locks at most one region for each region of the registry.
    ::fixy::FixedList<LockedRegion, HotRegionRegistry::max_regions> locked_{};
};

// A policy changes process-wide state through privileged system calls,
// and only start-up code does that.  The choice of the hot CPU reads
// sysfs and procfs, which can wait on the file system.  So the context
// must own Init, IO and Block, and a hot foreground or a drain context
// does not reach this surface.
template <class Ctx>
concept CtxFitsHardeningMint = ::foundation::effects::IsExecCtx<Ctx>
                            && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>
                            && ::fixy::CtxFitsFileOpen<Ctx>;

class Hardening {
public:
    // Hold the returned guard for as long as the policy is to stay in
    // effect. Setting CRUCIBLE_WARDEN_QUIET to 1 suppresses the
    // warnings that a skipped knob emits.
    template <class Ctx>
        requires CtxFitsHardeningMint<Ctx>
    [[nodiscard]] static AppliedPolicy apply(Ctx const& ctx, const Policy& p) noexcept {
        AppliedPolicy g;
        if (!p.hot_enabled) return g;

#ifdef __linux__
        {
            // The pin goes through the affinity door of fixy/os/Sched.h,
            // which records a new pin event, so an older pin proof of
            // this thread stops being in force.  The door reads the prior
            // mask first, and revert puts it back through the same door.
            const int cpu = select_hot_cpu(ctx, p.hot_core);
            if (cpu >= 0) {
                auto prior = ::fixy::sched::apply_affinity_to_cpu(ctx, cpu);
                if (prior) {
                    g.prior_affinity_ = ::fixy::Option<::fixy::sched::PriorAffinity>::some(std::move(*prior));
                    g.pinned_cpu_ = cpu;
                } else {
                    detail::warn("sched_getaffinity or sched_setaffinity", prior.error());
                }
            }
        }

        // A real-time class on a shared core starves everything else
        // that runs there, so it is skipped unless the core is
        // isolated. Setting CRUCIBLE_WARDEN_FORCE to 1 overrides this.
        bool realtime_allowed = true;
        if (p.hot_sched != SchedClass::Other && g.pinned_cpu_ >= 0) {
            const bool on_isolcpu = isolated_cpus(ctx).contains(g.pinned_cpu_);
            if (!on_isolcpu && !detail::is_env_set_to_one("CRUCIBLE_WARDEN_FORCE")) {
                ::fixy::report(::fixy::Sink::Err,
                               "[warden] CPU {} not isolated — skipping RT class "
                               "(set CRUCIBLE_WARDEN_FORCE=1 or boot isolcpus={}).\n",
                               g.pinned_cpu_, g.pinned_cpu_);
                realtime_allowed = false;
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
                        ::fixy::report(::fixy::Sink::Err,
                                       "[warden] SCHED_DEADLINE EPERM — fell back to "
                                       "SCHED_FIFO prio={}\n",
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
        (void)ctx;
#endif

        return g;
    }

    // A policy holds at most one locked region for each slot of the
    // registry, so a lock past that count is refused.
    [[nodiscard]] static bool lock_region(AppliedPolicy& g, void* addr, size_t len) noexcept {
#ifdef __linux__
        if (addr == nullptr || len == 0) return false;
        if (g.locked_.size() == HotRegionRegistry::max_regions) return false;
        // MLOCK_ONFAULT locks each page as it faults in rather than
        // faulting the whole region at once.  A kernel without mlock2 takes
        // the eager call instead.
        if (mlock2_sys(addr, len, MLOCK_ONFAULT) == 0 || ::mlock(addr, len) == 0) {
            static_cast<void>(g.locked_.push(AppliedPolicy::LockedRegion{.addr = addr, .len = len})
                                  .expect("the test above found a free entry for the region"));
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
        // A step of 4096 bytes touches each page also of a larger size.
        const size_t page =
            ::foundation::os::read_auxiliary(::foundation::os::AuxiliaryEntry::page_size).value_or(4096);
        volatile unsigned char* p = static_cast<unsigned char*>(addr);
        for (size_t off = 0; off < len; off += page)
            p[off] = p[off];
#else
        (void)addr;
        (void)len;
#endif
    }
};

// The privileged system calls this surface issues, named once at the
// type level so the set is discoverable and auditable. This is a
// classification, not a gate: admission is decided by the concept
// CtxFitsHardeningMint above Hardening.
//
// The set covers reads as well as writes. An auditor asking what this
// code does to the kernel is owed the whole answer, and apply() reads
// the prior affinity mask and the prior scheduling attributes before it
// replaces them. Leaving the two read halves out made the declared set
// smaller than the truth while every check beside it stayed green.
//
// The grant row of utils/rule-table.txt names this alias.  The rule
// syscalls of the quarantine plugin derives the set from the
// call sites in this header and fails when the two disagree, so the list is
// checked against the code rather than against its own prose.  The two
// affinity calls come through fixy::sched::apply_affinity_to_cpu, and the
// plugin follows the calls of that door through fixy/os/Sched.h and
// fixy/os/CpuPinned.h to find them.
using hardening_syscall_atoms =
    std::tuple<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::sched_setaffinity>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::sched_setattr>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::mlock>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::mlock2>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::munlock>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::madvise>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::prctl>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::sched_getaffinity>,
               ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::sched_getattr>>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHardeningMint<Ctx>
[[nodiscard]] inline AppliedPolicy mint_hardening(Ctx const& ctx, const Policy& policy) noexcept {
    return Hardening::apply(ctx, policy);
}

}  // namespace crucible::warden
