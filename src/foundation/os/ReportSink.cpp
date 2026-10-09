// The sink write of the Report family, which foundation/core/Report.h
// declares: the one write of the base to standard output or standard error,
// for report(), fatal(), unreachable() and the contract handler.
//
// The write issues the raw primitive directly.  A report can come from a
// signal handler, or while its thread holds the lock of the heap or of a
// stdio stream, and the primitive takes no lock and allocates nothing.  A
// failed write of a process that ends next has no caller to tell, so the
// write gives no Result.  In a build with ThreadSanitizer, the primitive gives
// the write to write(2) of the C library, so a write that blocks on a full
// pipe still runs the handler of a signal.  test_contract_handler shows this
// under the tsan preset.

#include <foundation/core/Report.h>

#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core::detail {

void write_sink_(Sink sink, char const* bytes, std::size_t count) noexcept {
    // The descriptors of standard output and standard error.  A value
    // outside the two enumerators writes to standard error.  The write must
    // not end the process, because the fatal exit writes through it.
    constexpr int kStandardOutput = 1;
    constexpr int kStandardError = 2;
    int const descriptor = sink == Sink::Out ? kStandardOutput : kStandardError;
    std::size_t written = 0;
    // write(2) can take fewer bytes than it was given, and a signal can end
    // it early.  A descriptor that refuses the bytes ends the write.
    bool has_stopped = false;
    while (!has_stopped && written < count) {
        os::result_of(os::raw_syscall(os::Syscall::write, descriptor, bytes + written, count - written))
            .match(
                [&](std::uint64_t taken) {
                    written += static_cast<std::size_t>(taken);
                    has_stopped = taken == 0;
                },
                [&](os::Errno error) { has_stopped = error != os::Errno::eintr; });
    }
}

}  // namespace foundation::core::detail
