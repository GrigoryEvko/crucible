#pragma once

// The argument words of the scheduling doors of fixy/os/Sched.h and
// fixy/os/CpuPinned.h: the numbers of the scheduler policies, the target of
// setpriority and the layout of struct sched_attr.
//
// The values are the values of <linux/sched.h> and <linux/resource.h>, and
// the layout of KernelSchedAttr is the layout of the first published struct
// sched_attr of <linux/sched/types.h>, on x86_64 and aarch64.  The door file
// src/foundation/os/Syscall.cpp compares each one with those headers, so the
// scheduling doors include no header of the kernel.
//
// This header is in the floor layer F0_os of utils/rule-table.txt.

#include <cstdint>

namespace foundation::os {

// The scheduler policies of sched_setscheduler and sched_setattr.  The kernel
// names the policy of a normal thread SCHED_NORMAL, and the C library names
// the same number SCHED_OTHER.
inline constexpr int kSchedNormal = 0;
inline constexpr int kSchedFifo = 1;
inline constexpr int kSchedRoundRobin = 2;
inline constexpr int kSchedBatch = 3;
inline constexpr int kSchedIdle = 5;
inline constexpr int kSchedDeadline = 6;

// The target kind of setpriority that names a thread by its id, and the
// calling thread by the id zero (PRIO_PROCESS).
inline constexpr int kPriorityOfThread = 0;

// The size of the first published struct sched_attr (SCHED_ATTR_SIZE_VER0).
// The kernel reads that many bytes when the field size holds this value.
inline constexpr std::uint32_t kSchedAttrSize = 48;

// The argument of sched_setattr, in the layout of the first published struct
// sched_attr.  The deadline fields are in nanoseconds.
struct KernelSchedAttr {
    std::uint32_t size = kSchedAttrSize;
    std::uint32_t policy = 0;
    std::uint64_t flags = 0;
    std::int32_t nice = 0;
    std::uint32_t priority = 0;
    std::uint64_t runtime_ns = 0;
    std::uint64_t deadline_ns = 0;
    std::uint64_t period_ns = 0;
};

}  // namespace foundation::os
