// The identity door of fixy (fixy/os/Process.h): the id of the calling
// process and of the calling thread.  It issues the raw primitive of
// foundation/os/Syscall.h, so this file includes no system header.  The
// kernel gives each id as a positive word, and the two calls cannot fail.

#include <fixy/os/Process.h>

#include <foundation/os/Syscall.h>

#include <cstdint>

namespace fixy::process::detail {

ProcessId current_process_() noexcept {
    namespace os = ::foundation::os;
    return static_cast<ProcessId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::getpid)));
}

ThreadId current_thread_() noexcept {
    namespace os = ::foundation::os;
    return static_cast<ThreadId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::gettid)));
}

}  // namespace fixy::process::detail
