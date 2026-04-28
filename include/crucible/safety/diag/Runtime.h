#pragma once
//
// safety/diag/Runtime.h — runtime cold-path violation emitter
// (FOUND-E06).  The runtime mirror of the compile-time diagnostic
// surface in safety/Diagnostic.h.
//
// ═════════════════════════════════════════════════════════════════════
// Pattern (28_04_2026_effects.md §7.1)
// ═════════════════════════════════════════════════════════════════════
//
// The compile-time diagnostic surface (RowMismatch / Insights /
// CheatProbe) catches violations that are decidable at template
// instantiation.  Some violations are decidable only at runtime:
//
//   * `Refined<P, T>` constructor's `pre()` clause — predicate
//     depends on a runtime value.
//   * `Monotonic<T>::advance` — current value must be ≥ previous.
//   * `Permission` linearity — runtime move-from-empty.
//   * Crash transport `OneShotFlag::peek` — peer-down at the
//     moment of inspection.
//
// For these, a runtime emitter is needed: write a STRUCTURED, MACHINE-
// PARSEABLE diagnostic to stderr, then let the caller decide whether
// to abort, retry, or fall through.
//
// ═════════════════════════════════════════════════════════════════════
// Surface
// ═════════════════════════════════════════════════════════════════════
//
//   * `report_violation(Category, fn_name, detail)` — cold-path
//     emitter.  Writes ONE LINE to stderr in machine-parseable form.
//     Does NOT abort (caller decides).  noexcept.
//
//   * `report_violation_and_abort(...)` — convenience that emits
//     and then std::abort()s.  Use for unrecoverable contract
//     violations.  [[noreturn]].
//
//   * `set_violation_sink(...)` — installation point for a custom
//     sink callable (e.g., route to a structured log instead of
//     stderr).  Production binaries override.
//
// ═════════════════════════════════════════════════════════════════════
// Output format (single line, machine-parseable)
// ═════════════════════════════════════════════════════════════════════
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>
//
// Example:
//
//   crucible-violation: category=RefinementViolation fn=mk_positive detail=value=-7 fails predicate `positive`
//
// Single-line, no embedded newlines, no embedded format characters.
// Detail strings are caller-provided; the emitter does NOT escape
// them (caller is responsible for sanitization at boundary sites).
//
// ═════════════════════════════════════════════════════════════════════
// Cost
// ═════════════════════════════════════════════════════════════════════
//
// COLD path.  `[[gnu::cold, gnu::noinline]]` on the symbol so the
// hot caller's icache stays clean.  fprintf is the worst case (~1 us);
// production binaries should override the sink to route to a faster
// channel (in-memory ring + bg drain).
//
// ═════════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>           // Category enum

#include <source_location>
#include <string_view>

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── violation_sink — pluggable runtime emitter ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Function-pointer type for the runtime sink.  Default sink writes
// to stderr; production binaries replace via `set_violation_sink`.
//
// Sink signature:
//   - `Category cat`        — the violation's classification
//   - `std::string_view fn` — function-name context (caller-supplied)
//   - `std::string_view dt` — detail context (caller-supplied)
//
// Sinks MUST be noexcept and async-signal-safe-aware (the runtime
// may invoke from contexts including signal handlers).

using violation_sink_t =
    void (*)(Category, std::string_view, std::string_view) noexcept;

// ── default_violation_sink — fprintf-to-stderr emitter ────────────
//
// Format:
//   crucible-violation: category=<name> fn=<fn> detail=<detail>\n
//
// Single-line, no embedded newlines from caller (caller is
// responsible).  Uses fprintf to stderr; this is NOT signal-safe but
// is async-signal-OK on Linux/glibc for the common case.
[[gnu::cold]]
void default_violation_sink(Category cat,
                            std::string_view fn,
                            std::string_view detail) noexcept;

// ── set_violation_sink — install a custom sink ────────────────────
//
// Returns the previous sink (allows scoped restoration).  Install
// at process startup for production routing; the bench harness
// installs a no-op sink to avoid stderr pollution during runs.
//
// Thread-safety: the sink atomic is a single pointer; install is
// `memory_order::release`, read is `memory_order::acquire`.
// Callers should install BEFORE spawning threads that may emit.
[[gnu::cold]]
violation_sink_t set_violation_sink(violation_sink_t sink) noexcept;

// ── current_violation_sink — read the active sink ─────────────────
[[nodiscard]] [[gnu::cold]]
violation_sink_t current_violation_sink() noexcept;

// ═════════════════════════════════════════════════════════════════════
// ── report_violation — emit a structured diagnostic ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Cold path.  Routes the violation through the active sink (default
// or installed).  Does NOT abort.  Caller decides recovery strategy.
//
// Pattern at a violation site:
//
//   if (!is_positive(value)) [[unlikely]] {
//       ::crucible::safety::diag::report_violation(
//           Category::RefinementViolation,
//           "mk_positive",
//           "value=-7 fails predicate `positive`");
//       return std::unexpected{Error::PredicateFailed};
//   }
[[gnu::cold]]
void report_violation(Category cat,
                      std::string_view fn,
                      std::string_view detail) noexcept;

// ═════════════════════════════════════════════════════════════════════
// ── report_violation_and_abort — emit + std::abort ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Convenience for unrecoverable contract violations.  Calls
// `report_violation`, then `std::abort()`.  Marked `[[noreturn]]` so
// the optimizer drops fall-through code at the call site.
//
// Use for:
//   * MemSafe / NullSafe / DetSafe axiom violations (recovery
//     impossible without state corruption)
//   * `Linear<T>` double-consume detected at runtime (UB if we let
//     the caller proceed)
//   * Reached an unreachable arm of a typestate machine (the type
//     system says we shouldn't be here)
[[gnu::cold]] [[noreturn]]
void report_violation_and_abort(Category cat,
                                std::string_view fn,
                                std::string_view detail) noexcept;

// ═════════════════════════════════════════════════════════════════════
// ── report_violation_at — source_location-capturing variant ────────
// ═════════════════════════════════════════════════════════════════════
//
// Convenience overload that captures the call site automatically via
// std::source_location.  The default arg is constructed at the CALL
// SITE (not inside the callee), so file/line/function are correct
// without macro tricks.  Zero runtime cost over the explicit form
// when the optimizer constant-folds the location's char* fields.
//
// Format (single line):
//   crucible-violation: category=<Name> fn=<file:line@function> detail=<detail>
//
// Where `fn` is the source_location-derived "<file>:<line>@<function>"
// composite — the parser script can split on `@` to recover the
// function name + file/line.

[[gnu::cold]]
void report_violation_at(
    Category cat,
    std::string_view detail,
    std::source_location loc = std::source_location::current()) noexcept;

[[gnu::cold]] [[noreturn]]
void report_violation_at_and_abort(
    Category cat,
    std::string_view detail,
    std::source_location loc = std::source_location::current()) noexcept;

}  // namespace crucible::safety::diag

// ═════════════════════════════════════════════════════════════════════
// ── CRUCIBLE_RUNTIME_VIOLATION macro — ergonomic wrapper ───────────
// ═════════════════════════════════════════════════════════════════════
//
// Drop-in convenience for the most common violation site: capture
// `__func__` + source_location automatically; caller just passes
// the Category and a detail string.
//
// Pattern at a violation site:
//
//   if (!is_positive(value)) [[unlikely]] {
//       CRUCIBLE_RUNTIME_VIOLATION(
//           ::crucible::safety::diag::Category::RefinementViolation,
//           "value=-7 fails predicate `positive`");
//       return std::unexpected{Error::PredicateFailed};
//   }
//
// Expands to a `report_violation_at(cat, detail)` call — the
// source_location default arg captures the call site automatically.
//
// `_AND_ABORT` variant aborts after emission (use for unrecoverable
// MemSafe / NullSafe / DetSafe axiom violations).

#define CRUCIBLE_RUNTIME_VIOLATION(category, detail) \
    ::crucible::safety::diag::report_violation_at((category), (detail))

#define CRUCIBLE_RUNTIME_VIOLATION_AND_ABORT(category, detail) \
    ::crucible::safety::diag::report_violation_at_and_abort((category), (detail))
