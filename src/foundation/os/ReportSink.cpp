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
//
// A write to standard error holds a block of SIGPIPE (THE PIPE SIGNAL of
// foundation/os/PipeSignal.h).  A report of an error must not end the process
// for a second reason, so a standard error with no reader gives epipe and no
// signal, and the write stops.  A write to standard output holds no block.  Standard
// output carries the output of the program, and a reader that closed it
// ends the program by the action of SIGPIPE that the process chose, as a
// pipe into head expects.  test_report_sink_pipe shows the two writes of a
// report and of a fatal exit.

#include <foundation/core/Report.h>

#include <foundation/os/Errno.h>
#include <foundation/os/PipeSignal.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core::detail {

namespace {

// The descriptors of standard output and standard error.
constexpr int kStandardOutput = 1;
constexpr int kStandardError = 2;

}  // namespace

void write_sink_(Sink sink, char const* bytes, std::size_t count) noexcept {
    // A value outside the two enumerators writes to standard error.  The
    // write must not end the process, because the fatal exit writes through
    // it.
    int const descriptor = sink == Sink::Out ? kStandardOutput : kStandardError;
    // Writes the bytes to the descriptor.  write(2) can take fewer bytes than
    // it was given, and a signal can end it early, so the loop writes again.
    // A descriptor that refuses the bytes ends the write.  The lambda gives
    // true when the descriptor refused them with epipe: a pipe or a socket
    // with no reader.
    auto const write_all = [descriptor, bytes, count]() noexcept -> bool {
        std::size_t written = 0;
        bool has_stopped = false;
        bool has_broken_pipe = false;
        while (!has_stopped && written < count) {
            os::result_of(os::raw_syscall(os::Syscall::write, descriptor, bytes + written, count - written))
                .match(
                    [&](std::uint64_t taken) {
                        written += static_cast<std::size_t>(taken);
                        has_stopped = taken == 0;
                    },
                    [&](os::Errno error) {
                        has_stopped = error != os::Errno::eintr;
                        has_broken_pipe = error == os::Errno::epipe;
                    });
        }
        return has_broken_pipe;
    };
    if (descriptor == kStandardOutput) {
        static_cast<void>(write_all());
        return;
    }
    os::PipeSignalBlock block = os::PipeSignalBlock::block_calling_thread();
    if (write_all()) {
        block.mark_broken_pipe();
    }
}

}  // namespace foundation::core::detail
