// The process doors of fixy (fixy/os/Process.h).  The identity door issues
// the raw primitive of foundation/os/Syscall.h: the kernel gives each id as a
// positive word, and the two calls cannot fail.  The child door reads the
// output of a child and waits for it through the steps of
// foundation/os/ChildProcess.h.  So this file includes no system header.

#include <fixy/os/Process.h>

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::process::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The bytes of one read of the output of a child.
constexpr std::size_t kReadChunk = 4096;

// The last kChildOutputCapacity bytes of a run of reads.  Each byte goes to
// the position of its count modulo the capacity, so a new byte replaces the
// oldest byte when the ring is full.
struct OutputRing {
    core::FixedArray<char, kChildOutputCapacity> bytes{};
    std::uint64_t total = 0;

    void put(core::View<char const> run) noexcept {
        for (char const character : run) {
            *bytes.get(total % kChildOutputCapacity).expect("a position modulo the capacity is in the ring") =
                character;
            ++total;
        }
    }
};

}  // namespace

core::Result<ChildReport, os::Errno> collect_child_(ChildParts parts) noexcept {
    OutputRing ring{};
    core::FixedArray<char, kReadChunk> chunk{};
    core::Option<os::Errno> read_error{};
    for (;;) {
        core::Result<std::size_t, os::Errno> taken = os::detail::read_child_output_(parts.error_output, chunk.view());
        if (taken.is_err()) {
            read_error = taken.err();
            break;
        }
        std::size_t const count = std::move(taken).value_or(0);
        if (count == 0) {
            break;
        }
        ring.put(core::View<char>{chunk.view()}.window(0, count).expect("a read gives at most the size of its buffer"));
    }
    os::detail::close_child_output_(parts.error_output);

    core::Result<os::ChildEnd, os::Errno> waited = os::detail::wait_child_(parts.child);
    if (read_error.is_some()) {
        return core::err(std::move(read_error).expect("the read error is present"));
    }
    if (waited.is_err()) {
        return core::err(waited.err().expect("the wait error is present"));
    }

    ChildReport report = ChildDoor::make_report();
    ChildDoor::set_end(report, std::move(waited).value_or(os::ChildEnd{}));
    bool const has_wrapped = ring.total > kChildOutputCapacity;
    std::size_t const kept = has_wrapped ? kChildOutputCapacity : ring.total;
    std::size_t const oldest = has_wrapped ? ring.total % kChildOutputCapacity : 0;
    ChildDoor::set_dropped_bytes(report, ring.total - kept);
    core::FixedText<kChildOutputCapacity>& output = ChildDoor::output(report);
    char* const text = core::detail::text_bytes_(output);
    for (std::size_t index = 0; index < kept; ++index) {
        text[index] = *ring.bytes.get((oldest + index) % kChildOutputCapacity)
                           .expect("a position modulo the capacity is in the ring");
    }
    core::detail::text_resize_(output, kept);
    return report;
}

ProcessId current_process_() noexcept {
    namespace os = ::foundation::os;
    return static_cast<ProcessId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::getpid)));
}

ThreadId current_thread_() noexcept {
    namespace os = ::foundation::os;
    return static_cast<ThreadId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::gettid)));
}

}  // namespace fixy::process::detail
