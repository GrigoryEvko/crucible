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

namespace detail::audit_findings_self_test {

enum class Probe : std::uint8_t {
    First = 1u << 0,
    Second = 1u << 1,
};

[[nodiscard]] consteval AuditFindings<Probe> error_then_warning() noexcept {
    AuditFindings<Probe> findings{};
    findings.raise(Probe::First, AuditSeverity::Error);
    findings.raise(Probe::Second, AuditSeverity::Warn);
    return findings;
}

static_assert(AuditFindings<Probe>{}.passes());
static_assert(error_then_warning().severity == AuditSeverity::Error);
static_assert(error_then_warning().has(Probe::First) && error_then_warning().has(Probe::Second));
static_assert(!error_then_warning().passes());

}  // namespace detail::audit_findings_self_test

}  // namespace crucible::cog
