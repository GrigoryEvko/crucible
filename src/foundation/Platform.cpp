// SPDX-License-Identifier: Apache-2.0

// The cold bodies of foundation/Platform.h: the probe of a tracer, and the
// failure arm of the assertion macros.
//
// The probe reads /proc/self/status through the file door of the base
// (foundation/os/File.h).  The failure arm writes its text through the sink
// write of the Report family (src/foundation/os/ReportSink.cpp).  This file
// includes no system header, calls no stdio and takes no lock, so each of its
// functions is safe in a signal handler.

#include <foundation/Platform.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/os/File.h>

#include <cstddef>

namespace foundation::detail {

namespace {

// The capacity of the text of /proc/self/status that the probe reads.  The
// kernel writes TracerPid in the first lines of the file, far below this
// capacity.
constexpr std::size_t status_bytes = 4096;

// The name of the field that the probe reads, with its colon.
constexpr core::TextView tracer_field{"TracerPid:"};

// The text of a failure.  The consteval constructor of Fmt makes sure that
// each placeholder has an argument of the given type.
constexpr core::Fmt<core::TextView, core::TextView, core::TextView, int> failure_text{
    "foundation: {} failed: {} ({}:{})\n"};

// True when the line text[start, end) starts with the name of the field
// TracerPid.
[[nodiscard]] bool is_tracer_line(char const* text, std::size_t start, std::size_t end) noexcept {
    char const* const field = core::detail::text_first_(tracer_field);
    std::size_t const field_size = tracer_field.size();
    if (end - start < field_size) return false;
    for (std::size_t index = 0; index < field_size; ++index) {
        if (text[start + index] != field[index]) return false;
    }
    return true;
}

// True when the field TracerPid of the status text holds a process id that
// is not zero.  The kernel writes the id as decimal digits after a tab, and
// it writes 0 when no tracer is attached.  A text with no such field gives
// false.  Complexity: O(the size of the text).
[[nodiscard]] bool names_a_tracer(core::TextView status) noexcept {
    char const* const text = core::detail::text_first_(status);
    std::size_t const size = status.size();
    std::size_t const field_size = tracer_field.size();
    std::size_t start = 0;
    while (start < size) {
        std::size_t end = start;
        while (end < size && text[end] != '\n')
            ++end;
        if (is_tracer_line(text, start, end)) {
            std::size_t cursor = start + field_size;
            while (cursor < end && (text[cursor] == ' ' || text[cursor] == '\t'))
                ++cursor;
            return cursor < end && text[cursor] >= '1' && text[cursor] <= '9';
        }
        start = end + 1;
    }
    return false;
}

// The argument of a report for a C string.  A null pointer is written as
// "(null)".  Complexity: O(the length of the string).
[[nodiscard]] core::detail::FmtArg text_argument(char const* text) noexcept {
    char const* const shown = text != nullptr ? text : "(null)";
    std::size_t length = 0;
    while (shown[length] != '\0')
        ++length;
    return core::detail::FmtArg{core::detail::FmtArgKind::text, 0, shown, length};
}

}  // namespace

bool is_debugger_present() noexcept {
    core::FixedText<status_bytes> status;
    if (os::read_file("/proc/self/status", status).is_err()) return false;
    return names_a_tracer(status.view());
}

void fail_invariant(char const* what, char const* predicate, char const* file, int line) noexcept {
    // The tracer is read one time.  Under a tracer, the arm stops at the
    // failure, and not inside abort.
    if (is_debugger_present()) __builtin_trap();
    core::detail::FmtArg const arguments[] = {text_argument(what), text_argument(predicate), text_argument(file),
                                              core::detail::fmt_arg_of_(line)};
    core::detail::report_(core::Sink::Err, core::detail::FmtDoor::text_of_(failure_text), arguments,
                          sizeof arguments / sizeof arguments[0]);
    __builtin_abort();
}

}  // namespace foundation::detail
