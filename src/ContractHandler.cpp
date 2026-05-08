// SPDX-License-Identifier: Apache-2.0
// crucible — src/ContractHandler.cpp
//
// Two violation entry points share one diagnostic + abort discipline:
//
//   1. `handle_contract_violation` — P2900R14 weak hook called by GCC's
//      contract runtime when a `pre()` / `post()` / `contract_assert`
//      clause fires under semantic={observe,enforce}.
//   2. `crucible::detail::contract_failed` — CRUCIBLE_PRE / CRUCIBLE_POST
//      / CRUCIBLE_PRE_FAST / CRUCIBLE_POST_FAST runtime branch entry
//      point (see safety/Pre.h).  Hand-rolled because we don't try to
//      construct a `std::contracts::contract_violation` object (its
//      `__impl*` field is libstdc++-internal).
//
// Both produce the same human-readable output and follow the same
// debugger-aware abort discipline.  Production Keeper/Vessel can
// override either with a strong symbol that routes into
// crucible_abort() for coordinated teardown.
//
// Output format (uniform across both paths):
//   crucible: contract violation: <comment>
//     at <file>:<line> in <function>
//     [optionally annotated message]
//     stack trace (most recent call first):
//        #0 <demangled fn> at <file>:<line>
//        #1 ...
//
// Stack trace via C++23 `<stacktrace>` (libstdc++ 16 with -lstdc++exp,
// which is already linked because we use `<contracts>`).  When trace
// capture costs more than the abort path can spend (security-sensitive
// crash where attacker may probe via SIGABRT timing), define
// CRUCIBLE_CONTRACT_NO_STACKTRACE at compile time.
//
// On unattended CI, breakpoint_if_debugging() no-ops and execution
// falls through to std::abort(), producing the core dump that
// post-mortem tooling expects.

#include <crucible/Platform.h>
#include <crucible/safety/Pre.h>   // crucible::detail::contract_failed declaration

#include <contracts>
#include <cstdio>
#include <cstdlib>

#if !defined(CRUCIBLE_CONTRACT_NO_STACKTRACE)
  #if __has_include(<stacktrace>) && defined(__cpp_lib_stacktrace)
    #include <stacktrace>
    #define CRUCIBLE_CONTRACT_HAS_STACKTRACE 1
  #endif
#endif

// ─── Internal helpers ──────────────────────────────────────────────
namespace {

[[gnu::cold]]
void emit_stack_trace_() noexcept {
#if defined(CRUCIBLE_CONTRACT_HAS_STACKTRACE)
    auto trace = std::stacktrace::current(/*skip=*/2);
    if (trace.empty()) return;
    std::fprintf(stderr, "  stack trace (most recent call first):\n");
    int depth = 0;
    for (auto const& entry : trace) {
        if (depth >= 16) break;     // bound at 16 frames; deeper traces
        auto desc = entry.description();   // are noise on a hot path
        auto file = entry.source_file();
        auto line = entry.source_line();
        std::fprintf(stderr, "    #%-2d %s",
                     depth,
                     desc.empty() ? "(unknown)" : desc.c_str());
        if (!file.empty() && line != 0) {
            std::fprintf(stderr, " at %s:%u",
                         file.c_str(), static_cast<unsigned>(line));
        }
        std::fputc('\n', stderr);
        ++depth;
    }
#endif
}

[[gnu::cold]]
void emit_violation_diagnostic(char const* comment,
                               char const* file,
                               unsigned line,
                               char const* fn,
                               char const* annotation = nullptr) noexcept {
    std::fprintf(stderr,
                 "crucible: contract violation: %s\n"
                 "  at %s:%u in %s\n",
                 comment ? comment : "(no comment)",
                 file ? file : "(unknown file)",
                 line,
                 fn ? fn : "(unknown function)");
    if (annotation && *annotation) {
        std::fprintf(stderr, "  note: %s\n", annotation);
    }
    emit_stack_trace_();
}

}  // namespace

// ─── P2900 weak hook ───────────────────────────────────────────────
extern "C++"
[[gnu::weak, noreturn]]
void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept;

[[gnu::weak, noreturn]]
void handle_contract_violation(
    const std::contracts::contract_violation& v) noexcept {
    const std::source_location loc = v.location();
    emit_violation_diagnostic(v.comment(),
                              loc.file_name(),
                              loc.line(),
                              loc.function_name(),
                              /*annotation=*/nullptr);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

// ─── CRUCIBLE_PRE / CRUCIBLE_POST runtime entry points ─────────────
// Two-arity surface: `contract_failed(expr, file, line, fn)` is the
// cheap form (predicate text only); `contract_failed_msg(expr, file,
// line, fn, msg)` is the annotated form for CRUCIBLE_PRE_MSG /
// CRUCIBLE_POST_MSG sites where the predicate alone is cryptic.
//
// Both are declared in safety/Pre.h and route through
// emit_violation_diagnostic so debug output is uniform regardless of
// which macro family or variant fired the violation.
namespace crucible::detail {

[[noreturn, gnu::cold]]
void contract_failed(char const* expr,
                     char const* file,
                     int line,
                     char const* fn) noexcept {
    emit_violation_diagnostic(expr, file, static_cast<unsigned>(line), fn,
                              /*annotation=*/nullptr);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

[[noreturn, gnu::cold]]
void contract_failed_msg(char const* expr,
                         char const* file,
                         int line,
                         char const* fn,
                         char const* msg) noexcept {
    emit_violation_diagnostic(expr, file, static_cast<unsigned>(line), fn, msg);
    ::crucible::detail::breakpoint_if_debugging();
    std::abort();
}

}  // namespace crucible::detail
