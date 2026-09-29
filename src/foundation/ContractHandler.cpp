// SPDX-License-Identifier: Apache-2.0

// The contract-violation handler for every binary that links foundation.
//
// libstdc++exp.a also holds a weak handle_contract_violation.  An object file
// with a contract check names the handler, so the linker takes this file from
// libfoundation.a first, and this definition wins.  The definition is weak
// too: a binary that wants a different failure policy overrides it with a
// strong one.  Every path ends in std::abort.
//
// A violation can occur in a signal handler, or while its thread holds the
// lock of the heap or of a stdio stream.  So the report calls only functions
// that POSIX makes async-signal-safe.  It writes to file descriptor 2 with
// write(2), through a small buffer on the stack.  The debugger probe of
// foundation/Platform.h uses open, read, close and strstr.  Nothing here
// calls stdio, allocates or takes a lock.  A core dump or a debugger gives
// the stack.
//
// A second violation on one thread while that thread reports a violation
// aborts at once.  It can come from a signal handler that interrupts the
// report, and a report of it could block on the same descriptor or recurse.

#include <foundation/Platform.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <contracts>
#include <cstddef>
#include <cstdlib>
#include <string_view>

#include <unistd.h>

namespace foundation::detail {

// Not zero while this thread reports a violation.  A signal handler on the
// same thread reads it, and volatile std::sig_atomic_t is the type whose
// accesses keep their program order for that reader.  With a plain bool, the
// compiler can move the store after the first write(2), because no other
// code of this file reads the flag.
//
// The flag is one per process, so a second violation in a different shared
// library that links foundation also sees the report in progress.  The
// object is inline, so that it has the vague linkage that the marker needs.
// No header declares it, so only this file names it.  The initial-exec model
// makes each access one load relative to the thread pointer, so no access
// can allocate the TLS block of a dynamically loaded module.  The flag takes
// a few bytes of the static TLS surplus of a library that the process loads
// later.
CRUCIBLE_PROCESS_WIDE [[gnu::tls_model(
    "initial-exec")]] inline constinit thread_local volatile std::sig_atomic_t contract_report_in_progress = 0;

}  // namespace foundation::detail

namespace {

// Collects the text of a report and writes it to file descriptor 2.  A full
// buffer is written before more text goes in, so the report is never cut.
class stderr_report {
public:
    stderr_report() noexcept = default;
    stderr_report(const stderr_report&) = delete("a report writes its text once");
    stderr_report& operator=(const stderr_report&) = delete("a report writes its text once");
    ~stderr_report() { flush_(); }

    void append(std::string_view text) noexcept {
        for (const char byte : text) {
            if (length_ == bytes_.size()) flush_();
            bytes_[length_] = byte;
            ++length_;
        }
    }

    void append_or(const char* text, std::string_view fallback) noexcept {
        append(text != nullptr ? std::string_view{text} : fallback);
    }

    void append_decimal(unsigned value) noexcept {
        std::array<char, 10> digits{};
        std::size_t count = 0;
        do {
            digits[count] = static_cast<char>('0' + value % 10U);
            ++count;
            value /= 10U;
        } while (value != 0U);
        while (count > 0) {
            --count;
            append(std::string_view{&digits[count], 1});
        }
    }

private:
    // write(2) can take fewer bytes than it was given, and a signal can end
    // it early.  A descriptor that refuses the bytes ends the report, because
    // the process aborts next in any case.
    void flush_() noexcept {
        std::size_t written = 0;
        while (written < length_) {
            const ::ssize_t
                taken =  // SYSCALL-CAP-OK: an async-signal-safe report from any context, a signal handler too
                ::write(STDERR_FILENO, bytes_.data() + written, length_ - written);
            if (taken > 0) {
                written += static_cast<std::size_t>(taken);
            } else if (taken < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        length_ = 0;
    }

    std::array<char, 256> bytes_{};
    std::size_t length_ = 0;
};

// Marks this thread as one that reports a violation.  A thread that is marked
// already meets its second violation, and it aborts at once.
void enter_report_or_abort() noexcept {
    if (::foundation::detail::contract_report_in_progress != 0) [[unlikely]]
        std::abort();
    ::foundation::detail::contract_report_in_progress = 1;
}

[[gnu::cold]]
void report_violation(char const* comment, char const* file, unsigned line, char const* function,
                      char const* note) noexcept {
    stderr_report report;
    report.append("foundation: contract violation: ");
    report.append_or(comment, "(no comment)");
    report.append("\n  at ");
    report.append_or(file, "(unknown file)");
    report.append(":");
    report.append_decimal(line);
    report.append(" in ");
    report.append_or(function, "(unknown function)");
    report.append("\n");
    if (note != nullptr && *note != '\0') {
        report.append("  note: ");
        report.append(note);
        report.append("\n");
    }
}

}  // namespace

extern "C++" [[gnu::weak, noreturn]]
void handle_contract_violation(const std::contracts::contract_violation& v) noexcept;

[[gnu::weak, noreturn]]
void handle_contract_violation(const std::contracts::contract_violation& v) noexcept {
    enter_report_or_abort();
    const std::source_location loc = v.location();
    report_violation(v.comment(), loc.file_name(), loc.line(), loc.function_name(), /*note=*/nullptr);
    ::foundation::detail::breakpoint_if_debugging();
    std::abort();
}

// The message forms CRUCIBLE_PRE_MSG and CRUCIBLE_POST_MSG call this.  A
// language contract clause has no message, and user code cannot make a
// std::contracts::contract_violation, because its implementation pointer
// is internal to the standard library.  So this function repeats the abort
// discipline of handle_contract_violation, and it does not call the handler.
namespace foundation::detail {

[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr, char const* file, int line, char const* fn, char const* msg) noexcept;

[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr, char const* file, int line, char const* fn, char const* msg) noexcept {
    enter_report_or_abort();
    report_violation(expr, file, static_cast<unsigned>(line), fn, msg);
    ::foundation::detail::breakpoint_if_debugging();
    std::abort();
}

}  // namespace foundation::detail
