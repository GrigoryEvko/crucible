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
#include <fixy/os/Pages.h>
#include <fixy/os/Sched.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>
#include <foundation/os/SchedWords.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace crucible::warden {

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

// The warning of a refused door call, with the error number of the kernel.
[[gnu::cold]] inline void warn(::fixy::TextView mechanism, ::foundation::os::Errno error) noexcept {
    warn(mechanism, ::foundation::os::error_number(error));
}

}  // namespace detail

// The row of the doors that undo a policy: the scheduling doors take Init,
// and the page doors take IO and Block.
using UndoRow = ::foundation::effects::Row<::foundation::effects::Effect::Init, ::foundation::effects::Effect::IO,
                                           ::foundation::effects::Effect::Block>;

// The context of the undo of a policy.  apply narrows its own context to
// this row and keeps it in the handle, so the destructor calls the doors
// that put each change back.
using UndoCtx = ::foundation::effects::ExecCtx<::foundation::effects::Init, UndoRow>;

class AppliedPolicy {
public:
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
    // Each record of a change clears immediately after its own undo
    // rather than relying on the guard at the top. Move-assignment
    // reverts and then swaps, so a record left set here would travel
    // into the moved-from handle and make it claim work it has already
    // undone.
    //
    // The revert is teardown work of system calls, so no caller inlines
    // it.  A caller pays one call, and a measurement loop keeps its code
    // small.
    CRUCIBLE_NOINLINE void revert() noexcept {
        if (reverted_) return;
        reverted_ = true;
        for (UndoCtx const& ctx : undo_ctx_) {
            undo_(ctx);
        }
        // Restoring the prior affinity mask retracts the claim that
        // this thread sits on one named CPU.
        pinned_cpu_ = -1;
    }

    [[nodiscard]] bool scheduler_applied() const noexcept { return prior_sched_.is_some(); }
    [[nodiscard]] bool affinity_applied() const noexcept { return prior_affinity_.is_some(); }
    [[nodiscard]] size_t regions_locked() const noexcept { return locked_.size(); }
    [[nodiscard]] int pinned_cpu() const noexcept { return pinned_cpu_; }

private:
    friend class Hardening;

    // Puts back each change in the order of apply: the scheduling
    // attributes, the affinity, the locks of the regions and the THP
    // setting of the process.
    void undo_(UndoCtx const& ctx) noexcept {
        for (::foundation::os::KernelSchedAttr const& prior : prior_sched_) {
            static_cast<void>(::fixy::sched::apply_sched_attributes(ctx, prior));
        }
        prior_sched_ = ::fixy::none;
        // The restore goes through the affinity door, so a pin proof of the
        // hot CPU stops being in force.  The match leaves the Option empty.
        std::move(prior_affinity_)
            .match([](::fixy::sched::PriorAffinity prior) noexcept { (void)std::move(prior).restore(); },
                   []() noexcept {});
        for (::fixy::View<std::byte const> const bytes : locked_) {
            static_cast<void>(::fixy::pages::unlock_pages(ctx, bytes));
        }
        locked_.clear();
        for (::fixy::pages::ThpSetting const prior : prior_thp_) {
            static_cast<void>(::fixy::pages::exchange_thp_setting(ctx, prior));
        }
        prior_thp_ = ::fixy::none;
    }

    // Moves the value of source into target, and leaves source empty.  The
    // match moves the value out of source, and the assignment takes a new
    // Option that is full.  A move assignment from source itself gives a
    // false maybe-uninitialized error of GCC 16 at -O3 with ASan, because
    // the compiler does not see that the slot of source is full.
    template <class T>
    static void take_option_(::fixy::Option<T>& target, ::fixy::Option<T>& source) noexcept {
        std::move(source).match([&target](T value) noexcept { target = ::fixy::Option<T>::some(std::move(value)); },
                                [&target]() noexcept { target = ::fixy::none; });
    }

    // Takes each change that source holds, and leaves source with no
    // change to undo.
    void take_(AppliedPolicy& source) noexcept {
        reverted_ = source.reverted_;
        take_option_(undo_ctx_, source.undo_ctx_);
        take_option_(prior_sched_, source.prior_sched_);
        take_option_(prior_affinity_, source.prior_affinity_);
        take_option_(prior_thp_, source.prior_thp_);
        pinned_cpu_ = source.pinned_cpu_;
        locked_ = std::move(source.locked_);
        source.pinned_cpu_ = -1;
        source.locked_.clear();
    }

    bool reverted_ = false;
    // The context of the undo.  Empty for a handle that holds no change.
    ::fixy::Option<UndoCtx> undo_ctx_{};
    // The scheduling attributes that the real-time class of apply replaced.
    ::fixy::Option<::foundation::os::KernelSchedAttr> prior_sched_{};
    // The mask that the pin of apply replaced, held while the pin stays.
    ::fixy::Option<::fixy::sched::PriorAffinity> prior_affinity_{};
    // The THP setting of the process before apply refused THP.
    ::fixy::Option<::fixy::pages::ThpSetting> prior_thp_{};
    int pinned_cpu_ = -1;
    // Apply locks at most one region for each region of the registry.
    ::fixy::FixedList<::fixy::View<std::byte const>, HotRegionRegistry::max_regions> locked_{};
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
        g.undo_ctx_ = ::fixy::Option<UndoCtx>::some(ctx.template in_row<UndoRow>());

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
            apply_sched_class_(ctx, p, g);
        }

        // This knob is process-wide, unlike the thread-scoped ones
        // above, and children inherit it.  The door gives the setting
        // before the call, and revert puts that setting back.
        if (p.disable_thp_global) {
            ::fixy::Result<::fixy::pages::ThpSetting, ::foundation::os::Errno> prior =
                ::fixy::pages::exchange_thp_setting(ctx, ::fixy::pages::ThpSetting::refused);
            for (::foundation::os::Errno const error : prior.err()) {
                detail::warn("prctl(PR_SET_THP_DISABLE)", error);
            }
            g.prior_thp_ = std::move(prior).ok();
        }

        // The three steps run in this order for each region. The huge
        // page hint has to be in place before the first fault, and a
        // collapse only succeeds once the pages are present.
        if (p.mlock_hot_regions || p.thp_hint_pools || p.thp_collapse_now) {
            const auto regions = HotRegionRegistry::instance().snapshot();
            for (const auto& r : regions) {
                if (p.thp_hint_pools && r.huge_hint) {
                    (void)hint_hugepage(ctx, r.bytes);
                }
                if (p.mlock_hot_regions) {
                    (void)lock_region(ctx, g, r.bytes);
                }
                if (p.thp_collapse_now && r.huge_hint) {
                    (void)collapse_hugepage(ctx, r.bytes);
                }
            }
        }

        return g;
    }

    // A policy holds at most one locked region for each slot of the
    // registry, so a lock past that count is refused.
    //
    // MLOCK_ONFAULT locks each page as it faults in rather than faulting
    // the whole region at once.  A kernel without mlock2 takes the eager
    // lock instead.
    template <class Ctx>
        requires CtxFitsHardeningMint<Ctx>
    [[nodiscard]] static bool lock_region(Ctx const& ctx, AppliedPolicy& g,
                                          ::fixy::View<std::byte const> bytes) noexcept {
        if (bytes.size() == 0) return false;
        if (g.locked_.size() == HotRegionRegistry::max_regions) return false;
        if (::fixy::pages::lock_pages_on_fault(ctx, bytes).is_err()) {
            ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> const locked = ::fixy::pages::lock_pages(ctx, bytes);
            for (::foundation::os::Errno const error : locked.err()) {
                detail::warn("mlock2/mlock", error);
                return false;
            }
        }
        static_cast<void>(g.locked_.push(bytes).expect("the test above found a free entry for the region"));
        return true;
    }

    // The door advises each whole huge page of the region.  From Linux 5.8
    // the advice call rejects an address that is not aligned to a huge
    // page, so the door rounds the range inward first.  A region with no
    // whole huge page gives false with no warning.
    template <class Ctx>
        requires CtxFitsHardeningMint<Ctx>
    [[nodiscard]] static bool hint_hugepage(Ctx const& ctx, ::fixy::View<std::byte const> bytes) noexcept {
        ::fixy::Result<std::size_t, ::foundation::os::Errno> const advised =
            ::fixy::pages::advise_huge_pages(ctx, bytes);
        for (::foundation::os::Errno const error : advised.err()) {
            detail::warn("madvise(MADV_HUGEPAGE)", error);
            return false;
        }
        return advised.match([](std::size_t count) noexcept { return count != 0; },
                             [](::foundation::os::Errno) noexcept { return false; });
    }

    // Linux 6.1 and later. The collapse happens before the call
    // returns, and the kernel emits a tracepoint an observer can count.
    template <class Ctx>
        requires CtxFitsHardeningMint<Ctx>
    [[nodiscard]] static bool collapse_hugepage(Ctx const& ctx, ::fixy::View<std::byte const> bytes) noexcept {
        ::fixy::Result<std::size_t, ::foundation::os::Errno> const collapsed =
            ::fixy::pages::collapse_huge_pages(ctx, bytes);
        for (::foundation::os::Errno const error : collapsed.err()) {
            // A kernel that does not know the advice reports ENOSYS or
            // EINVAL. That is expected and stays silent.
            if (error != ::foundation::os::Errno::enosys && error != ::foundation::os::Errno::einval) {
                detail::warn("madvise(MADV_COLLAPSE)", error);
            }
            return false;
        }
        return collapsed.match([](std::size_t count) noexcept { return count != 0; },
                               [](::foundation::os::Errno) noexcept { return false; });
    }

private:
    // The scheduling attributes of the real-time class of the policy.
    [[nodiscard]] static ::foundation::os::KernelSchedAttr sched_attributes_of_(const Policy& p) noexcept {
        ::foundation::os::KernelSchedAttr attributes{};
        switch (p.hot_sched) {
            case SchedClass::Fifo:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedFifo);
                attributes.priority = static_cast<std::uint32_t>(p.hot_rt_priority);
                break;
            case SchedClass::RoundRobin:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedRoundRobin);
                attributes.priority = static_cast<std::uint32_t>(p.hot_rt_priority);
                break;
            case SchedClass::Batch:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedBatch);
                break;
            case SchedClass::Idle:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedIdle);
                break;
            case SchedClass::Deadline:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedDeadline);
                attributes.runtime_ns = p.hot_runtime_ns;
                attributes.deadline_ns = p.hot_deadline_ns;
                attributes.period_ns = p.hot_period_ns;
                break;
            case SchedClass::Other:
            default:
                attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedNormal);
                break;
        }
        return attributes;
    }

    // The first-in-first-out class at the real-time priority of the policy.
    [[nodiscard]] static ::foundation::os::KernelSchedAttr fifo_attributes_of_(const Policy& p) noexcept {
        ::foundation::os::KernelSchedAttr attributes{};
        attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedFifo);
        attributes.priority = static_cast<std::uint32_t>(p.hot_rt_priority);
        return attributes;
    }

    // Reads the scheduling attributes of the thread, then sets the class of
    // the policy.  A thread whose attributes the door cannot read keeps
    // them, because revert could not put them back.
    template <class Ctx>
        requires CtxFitsHardeningMint<Ctx>
    static void apply_sched_class_(Ctx const& ctx, const Policy& p, AppliedPolicy& g) noexcept {
        ::fixy::Result<::foundation::os::KernelSchedAttr, ::foundation::os::Errno> prior =
            ::fixy::sched::read_sched_attributes(ctx);
        for (::foundation::os::Errno const error : prior.err()) {
            detail::warn("sched_getattr", error);
            return;
        }
        ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> const applied =
            ::fixy::sched::apply_sched_attributes(ctx, sched_attributes_of_(p));
        for (::foundation::os::Errno const error : applied.err()) {
            // From Linux 5.8 the deadline class refuses a thread whose
            // affinity is a strict subset of the deadline root domain,
            // which is exactly the pinning done above. Fall back to the
            // first-in-first-out class.
            if (p.hot_sched != SchedClass::Deadline || error != ::foundation::os::Errno::eperm) {
                detail::warn("sched_setattr (real-time class)", error);
                return;
            }
            ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> const fallback =
                ::fixy::sched::apply_sched_attributes(ctx, fifo_attributes_of_(p));
            for (::foundation::os::Errno const fallback_error : fallback.err()) {
                detail::warn("sched_setattr (FIFO fallback)", fallback_error);
                return;
            }
            ::fixy::report(::fixy::Sink::Err,
                           "[warden] SCHED_DEADLINE EPERM — fell back to "
                           "SCHED_FIFO prio={}\n",
                           p.hot_rt_priority);
        }
        g.prior_sched_ = std::move(prior).ok();
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
// checked against the code rather than against its own prose.  Each call
// comes through a door: the affinity and the scheduling calls through
// fixy/os/Sched.h and fixy/os/CpuPinned.h, and the page calls through
// fixy/os/Pages.h.  The plugin follows the calls of each door to find them.
using hardening_syscall_atoms =
    ::fixy::Pack<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::sched_setaffinity>,
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
