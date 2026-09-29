#pragma once

// The default sink emits one line per violation, in a shape external
// parsers depend on:
//
//   crucible-violation: category=<Name> fn=<fn> detail=<detail>
//
// Setting CRUCIBLE_DIAG_FORMAT=json in the environment switches the
// default sink to a JSON record instead. The default sink reads the
// environment once, on its first emission, so the variable must be set
// before the process starts.

#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>

#include <source_location>
#include <string_view>

namespace foundation::diag {

// report_violation is async-signal-safe only when its sink is.  The
// default sink is not, because it writes through stdio in each format.
// report_violation_at is not async-signal-safe with any sink, because it
// formats the position with snprintf.

using violation_sink_t = void (*)(Category, std::string_view, std::string_view) noexcept;

// The caller keeps the detail free of newlines. One violation is one
// line.
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
