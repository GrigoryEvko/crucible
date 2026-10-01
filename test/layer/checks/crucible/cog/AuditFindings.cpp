// The compile-time checks of crucible/cog/AuditFindings.h.

#include <crucible/cog/AuditFindings.h>

namespace crucible::cog {

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
