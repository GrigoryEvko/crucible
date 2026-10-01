#pragma once

// The shared shape of a read-only audit report. An audit raises issues
// one at a time, and the report keeps the set of issues and the worst
// severity that any of them raised.

#include <fixy/Bits.h>

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace crucible::cog {

// The order of the enumerators is the order of the severities, so the
// worst of two severities is their maximum.
enum class AuditSeverity : std::uint8_t {
    Pass = 0,
    Warn = 1,
    Error = 2,
};

template <typename Issue>
    requires std::is_scoped_enum_v<Issue>
struct AuditFindings {
    ::fixy::Bits<Issue> issues{};
    AuditSeverity severity = AuditSeverity::Pass;

    // A severity never decreases. A warning raised after an error leaves
    // the report at Error.
    constexpr void raise(Issue issue, AuditSeverity at) noexcept {
        issues.set(issue);
        severity = std::max(severity, at);
    }

    [[nodiscard]] constexpr bool passes() const noexcept { return severity == AuditSeverity::Pass && issues.none(); }

    [[nodiscard]] constexpr bool has(Issue issue) const noexcept { return issues.test(issue); }
};

}  // namespace crucible::cog
