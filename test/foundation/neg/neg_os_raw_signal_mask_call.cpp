// A door cannot change the signal mask of a thread through the raw
// primitive.  The block of SIGPIPE of foundation/os/PipeSignal.h issues
// rt_sigprocmask with a set that holds only SIGPIPE, and no enumerator of
// Syscall names the call.  So a raw call with a different set, such as a set
// that blocks a signal of the C library, does not compile.

#include <foundation/os/Syscall.h>

#include <cstdint>

namespace {

[[maybe_unused]] long block_each_signal() noexcept {
    std::uint64_t const each_signal = ~std::uint64_t{0};
    return ::foundation::os::raw_syscall(::foundation::os::Syscall::rt_sigprocmask, 0,
                                         static_cast<void const*>(&each_signal), 0, sizeof each_signal);
}

}  // namespace
