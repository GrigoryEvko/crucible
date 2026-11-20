// The door file of fixy/os/Stdio.h: the read of standard input.  It issues
// the raw read of foundation/os/Syscall.h on descriptor 0, so it includes no
// system header.

#include <fixy/os/Stdio.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace fixy::os::detail {

namespace {

namespace core = ::foundation::core;
namespace kernel = ::foundation::os;

// The descriptor of standard input.
constexpr int kStandardInput = 0;

}  // namespace

core::Result<std::size_t, kernel::Errno> read_standard_input_(char* first, std::size_t count) noexcept {
    std::size_t taken = 0;
    bool is_interrupted = true;
    bool has_failed = false;
    kernel::Errno failure = kernel::Errno::eio;
    while (is_interrupted) {
        is_interrupted = false;
        kernel::result_of(kernel::raw_syscall(kernel::Syscall::read, kStandardInput, first, count))
            .match([&](std::uint64_t bytes) { taken = static_cast<std::size_t>(bytes); },
                   [&](kernel::Errno error) {
                       is_interrupted = error == kernel::Errno::eintr;
                       has_failed = !is_interrupted;
                       failure = error;
                   });
    }
    if (has_failed) return core::err(failure);
    return taken;
}

}  // namespace fixy::os::detail
