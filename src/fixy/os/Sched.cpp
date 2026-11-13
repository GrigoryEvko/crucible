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

}  // namespace

// The kernel reads one int at the address of the priority: the struct
// sched_param of sched_setscheduler(2).
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setscheduler_sys(int policy, int rt_priority) noexcept {
    int const priority = rt_priority;
    return ::foundation::os::unit_of(::foundation::os::raw_syscall(
        ::foundation::os::Syscall::sched_setscheduler, kCallingThread, policy, static_cast<void const*>(&priority)));
}

// The kernel reads as many bytes as the field size names, so the door sets it
// to the size of the first published struct sched_attr.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sched_setattr_sys(::foundation::os::KernelSchedAttr const& attributes) noexcept {
    ::foundation::os::KernelSchedAttr sized = attributes;
    sized.size = ::foundation::os::kSchedAttrSize;
    return ::foundation::os::unit_of(::foundation::os::raw_syscall(
        ::foundation::os::Syscall::sched_setattr, kCallingThread, static_cast<void const*>(&sized), kNoFlags));
}

// The kernel writes at most the size argument of bytes, and it writes the
// size that it filled into the field size.
::foundation::core::Result<::foundation::os::KernelSchedAttr, ::foundation::os::Errno> sched_getattr_sys() noexcept {
    using Read = ::foundation::core::Result<::foundation::os::KernelSchedAttr, ::foundation::os::Errno>;
    ::foundation::os::KernelSchedAttr attributes{};
    long const word = ::foundation::os::raw_syscall(::foundation::os::Syscall::sched_getattr, kCallingThread,
                                                    static_cast<void const*>(&attributes),
                                                    ::foundation::os::kSchedAttrSize, kNoFlags);
    return ::foundation::os::result_of(word).match(
        [&attributes](std::uint64_t) -> Read { return attributes; },
        [](::foundation::os::Errno error) -> Read { return ::foundation::core::err(error); });
}

// The thread id zero names the calling thread, and the kernel sets the nice
// value of that thread only.
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> setpriority_sys(int nice) noexcept {
    return ::foundation::os::unit_of(::foundation::os::raw_syscall(
        ::foundation::os::Syscall::setpriority, ::foundation::os::kPriorityOfThread, kCallingThread, nice));
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
    return ::foundation::os::unit_of(::fixy::detail::sched_setaffinity_sys(mask_));
}

}  // namespace fixy::sched
