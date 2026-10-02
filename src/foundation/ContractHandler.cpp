// SPDX-License-Identifier: Apache-2.0

// The contract-violation handler for every binary that links foundation,
// and the cold report of fatal() and unreachable() of foundation/core.
//
// libstdc++exp.a also holds a weak handle_contract_violation.  An object file
// with a contract check names the handler, so the linker takes this file from
// libfoundation.a first, and this definition wins.  The definition is weak
// too: a binary that wants a different failure policy overrides it with a
// strong one.  Every path ends in std::abort.
//
// A violation can occur in a signal handler, or while its thread holds the
// lock of the heap or of a stdio stream.  So the report calls only functions
// that POSIX makes async-signal-safe.  It writes to standard error with
// write(2), through a small buffer on the stack.  The debugger probe of
// foundation/Platform.h uses open, read, close and strstr.  Nothing here
// calls stdio, allocates or takes a lock.  A core dump or a debugger gives
// the stack.
//
// This file also holds the sink write of the Report family: the one write
// to standard output or standard error of the base, for report(), fatal(),
// unreachable() and the contract handler.
//
// A second violation on one thread while that thread reports a violation
// aborts at once.  It can come from a signal handler that interrupts the
// report, and a report of it could block on the same descriptor or recurse.
// The thread leaves the report when its text is written, before the abort.
// So a test that catches the abort and continues gets the text of its next
// report too.

#include <foundation/Platform.h>
#include <foundation/core/Report.h>

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

// The sink write of the Report family, which foundation/core/Report.h
// declares.  It lives in this file because this file holds the one output
// door of the base: the one include of <unistd.h> for a write, and the one
// write(2) call.  The call goes through the write of the C library, and not
// through a raw system call, because the sanitizers intercept that write.
// ThreadSanitizer runs a signal handler only inside an intercepted call, so
// a report that blocks in a raw write never runs the handler of a signal.
// test_contract_handler shows this under the tsan preset.  The door of the
// Os family takes this function in Stage 4 of the quarantine plan.
namespace foundation::core::detail {

void write_sink_(Sink sink, char const* bytes, std::size_t count) noexcept {
    // A value outside the two enumerators writes to standard error.  The
    // write must not end the process, because the fatal exit writes through
    // it.
    int const descriptor = sink == Sink::Out ? STDOUT_FILENO : STDERR_FILENO;
    std::size_t written = 0;
    // write(2) can take fewer bytes than it was given, and a signal can end
    // it early.  A descriptor that refuses the bytes ends the write, because
    // a report has no road for the error.  The fatal exit aborts next in any
    // case.
    while (written < count) {
        ::ssize_t const taken =  // SYSCALL-CAP-OK: an async-signal-safe report from any context, a signal handler too
            ::write(descriptor, bytes + written, count - written);
        if (taken > 0) {
            written += static_cast<std::size_t>(taken);
        } else if (taken < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
}

}  // namespace foundation::core::detail

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

    // Appends the text of a report with its arguments, one window of the
    // formatter after the other, so no byte of a long text is lost.
    void append_formatted(char const* text, ::foundation::core::detail::FmtArg const* arguments,
                          std::size_t count) noexcept {
        constexpr std::size_t window_bytes = 128;
        char window[window_bytes] = {};
        std::size_t skip = 0;
        std::size_t length = 0;
        do {
            length = ::foundation::core::detail::format_window_(window, window_bytes, skip, text, arguments, count);
            std::size_t const rest = length - skip;
            std::size_t const taken = rest < window_bytes ? rest : window_bytes;
            append(std::string_view{window, taken});
            skip += taken;
        } while (skip < length);
    }

private:
    // The sink write writes again after a short write and after a signal.
    void flush_() noexcept {
        ::foundation::core::detail::write_sink_(::foundation::core::Sink::Err, bytes_.data(), length_);
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

// Marks this thread as one that no longer reports a violation.  The caller
// calls it after the text of its report is written, and then it aborts.  A
// signal that arrives between the two meets no report in progress, so its
// report is written too.
void leave_report() noexcept { ::foundation::detail::contract_report_in_progress = 0; }

// Writes the text of a fatal report with its arguments, and the site.
void report_fatal_text(char const* text, char const* file, std::uint32_t line, char const* function,
                       ::foundation::core::detail::FmtArg const* arguments, std::size_t count) noexcept {
    stderr_report report;
    report.append("foundation: fatal: ");
    if (text != nullptr) {
        report.append_formatted(text, arguments, count);
    } else {
        report.append("(no text)");
    }
    report.append("\n  at ");
    report.append_or(file, "(unknown file)");
    report.append(":");
    report.append_decimal(line);
    report.append(" in ");
    report.append_or(function, "(unknown function)");
    report.append("\n");
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
    leave_report();
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
    leave_report();
    ::foundation::detail::breakpoint_if_debugging();
    std::abort();
}

}  // namespace foundation::detail

// The cold arms of fatal() and unreachable().  The texts are constant
// arrays that the consteval constructors of Fmt and Site checked, so each
// one ends with a zero byte, and each placeholder has its argument.
namespace foundation::core::detail {

[[noreturn, gnu::cold]]
void report_fatal_(char const* text, char const* file, std::uint32_t line, char const* function) noexcept {
    enter_report_or_abort();
    report_fatal_text(text, file, line, function, nullptr, 0);
    leave_report();
    ::foundation::detail::breakpoint_if_debugging();
    std::abort();
}

[[noreturn, gnu::cold]]
void report_fatal_with_(char const* text, char const* file, std::uint32_t line, char const* function,
                        FmtArg const* arguments, std::size_t count) noexcept {
    enter_report_or_abort();
    report_fatal_text(text, file, line, function, arguments, count);
    leave_report();
    ::foundation::detail::breakpoint_if_debugging();
    std::abort();
}

}  // namespace foundation::core::detail
