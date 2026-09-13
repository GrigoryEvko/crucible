// SPDX-License-Identifier: Apache-2.0

#include <crucible/Platform.h>
#include <crucible/safety/Pre.h>

#include <contracts>
#include <cstdio>
#include <cstdlib>

#if !defined(CRUCIBLE_CONTRACT_NO_STACKTRACE)
#if __has_include(<stacktrace>) && defined(__cpp_lib_stacktrace)
#include <stacktrace>
#define CRUCIBLE_CONTRACT_HAS_STACKTRACE 1
#endif
#endif

namespace {

[[gnu::cold]]
void emit_stack_trace_() noexcept {
#if defined(CRUCIBLE_CONTRACT_HAS_STACKTRACE)
    auto trace = std::stacktrace::current(/*skip=*/2);
    if (trace.empty()) return;
    std::fprintf(stderr, "  stack trace (most recent call first):\n");
    int depth = 0;
    for (auto const& entry : trace) {
        if (depth >= 16) break;
        auto desc = entry.description();
        auto file = entry.source_file();
        auto line = entry.source_line();
        std::fprintf(stderr, "    #%-2d %s", depth, desc.empty() ? "(unknown)" : desc.c_str());
        if (!file.empty() && line != 0) {
            std::fprintf(stderr, " at %s:%u", file.c_str(), static_cast<unsigned>(line));
        }
        std::fputc('\n', stderr);
        ++depth;
    }
#endif
}

[[gnu::cold]]
void emit_violation_diagnostic(char const* comment, char const* file, unsigned line, char const* fn,
                               char const* annotation = nullptr) noexcept {
    std::fprintf(stderr,
                 "crucible: contract violation: %s\n"
                 "  at %s:%u in %s\n",
                 comment ? comment : "(no comment)", file ? file : "(unknown file)", line,
                 fn ? fn : "(unknown function)");
    if (annotation && *annotation) {
        std::fprintf(stderr, "  note: %s\n", annotation);
    }
    emit_stack_trace_();
}

}  // namespace

extern "C++" [[gnu::weak, noreturn]]
void handle_contract_violation(const std::contracts::contract_violation& v) noexcept;

[[gnu::weak, noreturn]]
void handle_contract_violation(const std::contracts::contract_violation& v) noexcept {
    const std::source_location loc = v.location();
    emit_violation_diagnostic(v.comment(), loc.file_name(), loc.line(), loc.function_name(),
                              /*annotation=*/nullptr);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

// These duplicate the abort discipline of handle_contract_violation rather
// than calling it.  A std::contracts::contract_violation cannot be built by
// user code, because its implementation pointer is internal to the standard
// library.
namespace crucible::detail {

[[noreturn, gnu::cold]]
void contract_failed(char const* expr, char const* file, int line, char const* fn) noexcept {
    emit_violation_diagnostic(expr, file, static_cast<unsigned>(line), fn,
                              /*annotation=*/nullptr);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr, char const* file, int line, char const* fn, char const* msg) noexcept {
    emit_violation_diagnostic(expr, file, static_cast<unsigned>(line), fn, msg);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

}  // namespace crucible::detail
