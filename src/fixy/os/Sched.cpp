// The scheduling door of fixy/os/Sched.h: the policy and the nice value of
// the calling thread, through the raw system call primitive of
// foundation/os/Syscall.h, and the restore of an affinity mask that a pin
// replaced, through the affinity door of fixy/os/CpuPinned.h.
//
// The argument words come from foundation/os/SchedWords.h, which the door
// file of the primitive compares with the kernel headers, so this file
// includes no header of the kernel and no header of the C library.

#include <fixy/os/Sched.h>

#include <foundation/os/Errno.h>
#include <foundation/os/SchedWords.h>
#include <foundation/os/Syscall.h>

#include <cstdint>

namespace fixy::sched::detail {

namespace {

// The thread id that names the calling thread in a scheduling call.
constexpr int kCallingThread = 0;

// The flags of a call of sched_setattr: none.
constexpr unsigned kNoFlags = 0;

// The result of a call whose value is zero on success.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
unit_of(long answer) noexcept {
    return ::foundation::os::result_of(answer).match(
        [](std::uint64_t) noexcept -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            return ::foundation::core::Unit{};
        },
        [](::foundation::os::Errno error) noexcept
            -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            return ::foundation::core::err(error);
        });
}

}  // namespace

// The kernel reads one int at the address of the priority: the struct
// sched_param of sched_setscheduler(2).
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setscheduler_sys(int policy, int rt_priority) noexcept {
    int const priority = rt_priority;
    return unit_of(::foundation::os::raw_syscall(::foundation::os::Syscall::sched_setscheduler, kCallingThread, policy,
                                                 static_cast<void const*>(&priority)));
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setattr_sys(std::uint64_t runtime_ns, std::uint64_t deadline_ns, std::uint64_t period_ns) noexcept {
    ::foundation::os::KernelSchedAttr attributes{};
    attributes.policy = static_cast<std::uint32_t>(::foundation::os::kSchedDeadline);
    attributes.runtime_ns = runtime_ns;
    attributes.deadline_ns = deadline_ns;
    attributes.period_ns = period_ns;
    return unit_of(::foundation::os::raw_syscall(::foundation::os::Syscall::sched_setattr, kCallingThread,
                                                 static_cast<void const*>(&attributes), kNoFlags));
}

// The thread id zero names the calling thread, and the kernel sets the nice
// value of that thread only.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> setpriority_sys(int nice) noexcept {
    return unit_of(::foundation::os::raw_syscall(::foundation::os::Syscall::setpriority,
                                                 ::foundation::os::kPriorityOfThread, kCallingThread, nice));
}

}  // namespace fixy::sched::detail

namespace fixy::sched {

// A holder with no mask changes nothing.  The affinity door sets the mask of
// the calling thread only, so a holder of a different thread refuses with
// EPERM and keeps its mask.  The holder is empty before the door call, so a
// refused call of the kernel restores nothing a second time.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> PriorAffinity::restore_mask_() noexcept {
    if (!is_held_) {
        return ::foundation::core::Unit{};
    }
    if (owner_ != ::foundation::effects::detail::producer_claim::calling_thread_identity()) {
        return ::foundation::core::err(::foundation::os::Errno::eperm);
    }
    is_held_ = false;
    return ::fixy::detail::sched_setaffinity_sys(mask_).match(
        [](std::uint64_t) noexcept -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            return ::foundation::core::Unit{};
        },
        [](::foundation::os::Errno error) noexcept
            -> ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> {
            return ::foundation::core::err(error);
        });
}

}  // namespace fixy::sched
