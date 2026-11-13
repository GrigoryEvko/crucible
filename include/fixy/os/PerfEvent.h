#pragma once

// The perf event door of fixy: the open of one event of the perf events of
// the kernel (perf_event_open), and the read of the count of an event.
//
// mint_perf_event opens one event of the user space of one task, on each CPU
// that runs the task.  An event with a period takes a sample at each period
// events, and a consumer, such as a BPF program that the kernel runs at each
// sample, reads its descriptor.  An event with no period is a counter, and
// read_count gives its count.  The event starts enabled, so the door sends no
// ioctl.  The descriptor does not survive execve (PERF_FLAG_FD_CLOEXEC).
//
// The door file src/fixy/os/PerfEvent.cpp issues the calls with the raw
// primitive of foundation/os/Syscall.h, so this header includes no system
// header.  detail::EventAttributes holds the first published layout of the
// struct perf_event_attr of the kernel, and the test of the door compares
// its bytes with the struct of <linux/perf_event.h>.  perf_event_open is a
// feature call of foundation/os/KernelFloor.h: a kernel with no perf events
// gives the error enosys to the caller.

#include <fixy/Qtt.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Process.h>
#include <foundation/NoObject.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <utility>

namespace fixy::perf {

namespace eff = ::foundation::effects;

// An event: the type word and the config word of struct perf_event_attr.
// The kernel gives the type of a dynamic PMU, such as an IBS unit of AMD, in
// sysfs, so the type is a number and not a closed enumeration.
struct Event final {
    std::uint32_t type = 0;
    std::uint64_t config = 0;
};

// The task that an event follows: one thread.  The kernel reads the id of a
// process as the id of its first thread, so the event of a ProcessId follows
// the first thread and not the other threads of the process.
class Task final {
    std::uint32_t id_ = 0;

public:
    constexpr Task(::fixy::process::ProcessId process) noexcept : id_{std::to_underlying(process)} {}
    constexpr Task(::fixy::process::ThreadId thread) noexcept : id_{std::to_underlying(thread)} {}

    [[nodiscard]] constexpr std::uint32_t id() const noexcept { return id_; }
};

// The hardware events of the door: configs of the type PERF_TYPE_HARDWARE.
enum class HardwareEvent : std::uint64_t {
    instructions = 1,
    branch_misses = 5,
};

// The software events of the door: configs of the type PERF_TYPE_SOFTWARE.
enum class SoftwareEvent : std::uint64_t {
    cpu_migrations = 4,
    major_page_faults = 6,
    alignment_faults = 7,
};

// The caches of a cache event: cache ids of the type PERF_TYPE_HW_CACHE.
enum class Cache : std::uint8_t {
    last_level = 2,
    data_tlb = 3,
};

namespace detail {

// The words of <linux/perf_event.h> that the door uses.  The test of the
// door compares each word with the word of the kernel header.
inline constexpr std::uint32_t kTypeHardware = 0;  // PERF_TYPE_HARDWARE
inline constexpr std::uint32_t kTypeSoftware = 1;  // PERF_TYPE_SOFTWARE
inline constexpr std::uint32_t kTypeHardwareCache = 3;  // PERF_TYPE_HW_CACHE
inline constexpr std::uint64_t kCacheOperationRead = 0;  // PERF_COUNT_HW_CACHE_OP_READ
inline constexpr std::uint64_t kCacheResultMiss = 1;  // PERF_COUNT_HW_CACHE_RESULT_MISS
inline constexpr std::uint32_t kAttributesSize = 64;  // PERF_ATTR_SIZE_VER0
inline constexpr std::uint64_t kExcludeKernel = std::uint64_t{1} << 5;  // the bit-field exclude_kernel
inline constexpr std::uint64_t kExcludeHypervisor = std::uint64_t{1} << 6;  // the bit-field exclude_hv
inline constexpr std::uint64_t kCloseOnExec = std::uint64_t{1} << 3;  // PERF_FLAG_FD_CLOEXEC

// The first published layout of struct perf_event_attr (PERF_ATTR_SIZE_VER0).
// The kernel reads the size word, and it takes each field after these 64
// bytes as zero.  flags holds the bit-fields of the kernel struct from
// disabled up, and the platform is little-endian, so the first bit-field is
// bit 0.
struct EventAttributes final {
    std::uint32_t type = 0;
    std::uint32_t size = kAttributesSize;
    std::uint64_t config = 0;
    std::uint64_t sample_period = 0;
    std::uint64_t sample_type = 0;
    std::uint64_t read_format = 0;
    std::uint64_t flags = 0;
    std::uint32_t wakeup_events = 0;
    std::uint32_t breakpoint_type = 0;
    std::uint64_t config1 = 0;
};

// The attributes of event in user space, with one sample each period events,
// or no sample for a period of zero.  The event starts enabled, and it counts
// no event of the kernel and of the hypervisor.
[[nodiscard]] constexpr EventAttributes user_sample_attributes(Event event, std::uint64_t period) noexcept {
    return EventAttributes{
        .type = event.type,
        .config = event.config,
        .sample_period = period,
        .flags = kExcludeKernel | kExcludeHypervisor,
    };
}

// Opens the event of attributes for the task with the id task on each CPU,
// with no group and with PERF_FLAG_FD_CLOEXEC, and gives the descriptor.  A
// call of the door file.  PerfEventDoor is the one caller that keeps the
// descriptor.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno> open_event_(EventAttributes const& attributes,
                                                                                   std::uint32_t task) noexcept;

// The count of the event of descriptor: the one word of a read with no read
// format.  A call of the door file.
[[nodiscard]] ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> read_count_(int descriptor) noexcept;

}  // namespace detail

// A hardware event.
[[nodiscard]] constexpr Event hardware_event(HardwareEvent event) noexcept {
    return Event{.type = detail::kTypeHardware, .config = std::to_underlying(event)};
}

// A software event.
[[nodiscard]] constexpr Event software_event(SoftwareEvent event) noexcept {
    return Event{.type = detail::kTypeSoftware, .config = std::to_underlying(event)};
}

// A read that misses cache.  The config packs the cache in its first byte,
// the operation in its second byte and the result in its third byte.
[[nodiscard]] constexpr Event cache_read_miss(Cache cache) noexcept {
    return Event{.type = detail::kTypeHardwareCache,
                 .config = std::uint64_t{std::to_underlying(cache)} | (detail::kCacheOperationRead << 8U)
                         | (detail::kCacheResultMiss << 16U)};
}

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts perf_event_open in
// the privilege family, which lifts to IO and Block.
using perf_event_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::perf_event_open>>;

template <typename Ctx>
concept CtxFitsPerfEventMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, perf_event_row_t>;

// Declared here and defined below PerfEventDoor, because PerfEventDoor names
// it as its friend, and a friend must already have been declared.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPerfEventMint<Ctx>
[[nodiscard]] inline ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>
mint_perf_event(Ctx const&, Event event, std::uint64_t period, Task task) noexcept;

// The door to perf_event_open.  No object of it exists.  Its member is
// private, and its one friend is mint_perf_event.  OwnedFd befriends this
// class, so the handle of an event comes only from mint_perf_event, after
// its gate.
class PerfEventDoor final : ::foundation::NoObject<PerfEventDoor> {
    template <eff::IsExecCtx FriendCtx>
        requires CtxFitsPerfEventMint<FriendCtx>
    friend auto mint_perf_event(FriendCtx const&, Event, std::uint64_t, Task) noexcept
        -> ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>;

    // Returns the error of the kernel on failure and no handle.
    [[nodiscard]] static ::foundation::core::Result<::fixy::fs::OwnedFd, ::foundation::os::Errno>
    open_(Event event, std::uint64_t period, Task task) noexcept {
        using Opened = ::foundation::core::Result<::fixy::fs::OwnedFd, ::foundation::os::Errno>;
        return detail::open_event_(detail::user_sample_attributes(event, period), task.id())
            .match([](int descriptor) -> Opened { return ::fixy::fs::OwnedFd{descriptor}; },
                   [](::foundation::os::Errno error) -> Opened { return ::foundation::core::err(error); });
    }
};

// Opens event in the user space of task, on each CPU that runs it.  A period
// above zero takes one sample each period events, and a period of zero opens
// a counter that takes no sample.  The error of the kernel tells why an open
// failed: eacces or eperm for a host whose perf_event_paranoid or
// capabilities refuse the event, enoent or eopnotsupp for an event that no
// PMU of the host has, esrch for no such task, and enosys for a kernel with
// no perf events.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPerfEventMint<Ctx>
[[nodiscard]] inline ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>
mint_perf_event(Ctx const&, Event event, std::uint64_t period, Task task) noexcept {
    using Minted = ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>;
    return PerfEventDoor::open_(event, period, task)
        .match(
            [](::fixy::fs::OwnedFd descriptor) -> Minted {
                return mint_linear<::fixy::fs::OwnedFd>(std::move(descriptor));
            },
            [](::foundation::os::Errno error) -> Minted { return ::foundation::core::err(error); });
}

// The read of a count is a read of a descriptor, which the catalog puts in the
// file family, and that family lifts to IO and Block.  The read of a perf
// event never waits.
using perf_count_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::read>>;

template <typename Ctx>
concept CtxFitsPerfCountRead = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, perf_count_row_t>;

// The count of a counter that mint_perf_event opened with a period of zero:
// the events of its task in user space since the open.  A closed handle
// gives ebadf.
template <eff::IsExecCtx Ctx>
    requires CtxFitsPerfCountRead<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
read_count(Ctx const&, ::fixy::fs::OwnedFd const& counter) noexcept {
    return detail::read_count_(counter.get());
}

}  // namespace fixy::perf
