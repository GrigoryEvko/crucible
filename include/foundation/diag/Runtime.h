#pragma once

// The default sink emits one line per violation, in a shape external
// parsers depend on:
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>
//
// A program that wants the JSON record of foundation/diag/JsonEmitter.h
// installs a sink with set_violation_sink, and that sink calls
// emit_json_violation.  The program chooses the format.  The base reads no
// environment variable for the choice, because a read of the environment is
// an effect that the call site of a violation does not show.

#include <foundation/Platform.h>
#include <foundation/diag/Tag.h>

#include <source_location>
#include <string_view>

namespace foundation::diag {

// report_violation is async-signal-safe only when its sink is.  The default
// sink is, because it writes through the sink write of the Report family
// (foundation/core/Report.h), with no stdio, no lock and no allocation.
// report_violation_at builds the position on its stack with no stdio, and it
// is async-signal-safe when its sink is.
//
// The views that a sink gets are valid while the sink runs.  A sink that
// keeps a field after it returns copies the characters.

using violation_sink_t = void (*)(Category, std::string_view, std::string_view) noexcept;

// Writes the line of a violation to standard error.  The caller keeps the
// detail free of newlines.  One violation is one line.
[[gnu::cold]]
void default_violation_sink(Category cat, std::string_view fn, std::string_view detail) noexcept;

// Returns the sink installed before this call, which lets a caller
// restore it. The sink pointer is stored with release ordering and read
// with acquire ordering. Install it before spawning any thread that can
// emit.
[[gnu::cold]]
violation_sink_t set_violation_sink(violation_sink_t sink) noexcept;

[[nodiscard]] [[gnu::cold]]
violation_sink_t current_violation_sink() noexcept;

[[gnu::cold]]
void report_violation(Category cat, std::string_view fn, std::string_view detail) noexcept;

[[gnu::cold]] [[noreturn]]
void report_violation_and_abort(Category cat, std::string_view fn, std::string_view detail) noexcept;

// These fill the `fn` field with the composite
// "<file>:<line>:<column>@<function>". A parser splits it on `@` to
// recover the function name and the position.

[[gnu::cold]]
void report_violation_at(Category cat, std::string_view detail,
                         std::source_location loc = std::source_location::current()) noexcept;

[[gnu::cold]] [[noreturn]]
void report_violation_at_and_abort(Category cat, std::string_view detail,
                                   std::source_location loc = std::source_location::current()) noexcept;

}  // namespace foundation::diag

// Report a violation of one Category at the location of the macro.
#define CRUCIBLE_DIAG_RUNTIME_VIOLATION(category, detail) ::foundation::diag::report_violation_at((category), (detail))

#define CRUCIBLE_DIAG_RUNTIME_VIOLATION_AND_ABORT(category, detail) \
    ::foundation::diag::report_violation_at_and_abort((category), (detail))
