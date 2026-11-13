// The perf event door of fixy (fixy/os/PerfEvent.h): the open of one event of
// the perf events of the kernel, and the read of a count.  It issues the raw
// primitive of foundation/os/Syscall.h, so this file includes no system
// header.  The test of the door compares the record of the attributes and
// the words of the header with <linux/perf_event.h>.  A call that a signal
// interrupts runs again.

#include <fixy/os/PerfEvent.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstdint>
#include <utility>

namespace fixy::perf::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The CPU word of an event that follows its process on each CPU, and the
// group word of an event that leads no group and joins none.
constexpr int kEachCpu = -1;
constexpr int kNoGroup = -1;

// The word of a call that a signal interrupted.
constexpr long kInterrupted = -static_cast<long>(std::to_underlying(os::Errno::eintr));

}  // namespace

core::Result<int, os::Errno> open_event_(EventAttributes const& attributes, std::uint32_t task) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::perf_event_open, static_cast<void const*>(&attributes), task, kEachCpu,
                               kNoGroup, kCloseOnExec);
    } while (word == kInterrupted);
    return os::result_of(word).match(
        [](std::uint64_t descriptor) -> core::Result<int, os::Errno> { return static_cast<int>(descriptor); },
        [](os::Errno error) -> core::Result<int, os::Errno> { return core::err(error); });
}

core::Result<std::uint64_t, os::Errno> read_count_(int descriptor) noexcept {
    using Count = core::Result<std::uint64_t, os::Errno>;
    if (descriptor < 0) {
        return core::err(os::Errno::ebadf);
    }
    std::uint64_t count = 0;
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::read, descriptor, static_cast<void const*>(&count), sizeof(count));
    } while (word == kInterrupted);
    // The kernel writes the whole word or gives an error, so a shorter read
    // is no count.
    return os::result_of(word).match(
        [&count](std::uint64_t bytes) -> Count {
            if (bytes != sizeof(count)) return core::err(os::Errno::eio);
            return count;
        },
        [](os::Errno error) -> Count { return core::err(error); });
}

}  // namespace fixy::perf::detail
