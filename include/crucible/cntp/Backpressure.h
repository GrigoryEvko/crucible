#pragma once

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/effects/Resources.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

// An error has no spelling of its own, so its name is its enumerator's
// identifier, read by foundation::reflect::enum_name.
enum class BackpressureError : std::uint8_t {
    InvalidCreditBytes,
    InvalidConnectionLimit,
    InvalidResourcePressure,
    InvalidResourceLimit,
    CreditFlowNotStarted,
    CreditExhausted,
    CreditOverflow,
    TooManyCreditFlows,
    TooManyResourceLimits,
    ConnectionLimitReached,
    ResourceLimitReached,
    AdmissionRejected,
};

enum class AdmissionDecisionKind : std::uint8_t {
    Accepted,
    RejectedBackoff,
    RejectedResource,
};

// The spelling of each outcome that an operator log line uses.
[[nodiscard]] constexpr std::string_view admission_decision_kind_name(AdmissionDecisionKind kind) noexcept {
    switch (kind) {
        case AdmissionDecisionKind::Accepted:
            return "accepted";
        case AdmissionDecisionKind::RejectedBackoff:
            return "rejected_backoff";
        case AdmissionDecisionKind::RejectedResource:
            return "rejected_resource";
        default:
            return "unknown";
    }
}

// One part in a million is the unit of every pressure and every limit.
// A pressure can be zero.  A limit cannot, because a limit of zero
// refuses every connection.
inline constexpr std::uint32_t kPpmFull = 1000000u;
inline constexpr auto resource_pressure_ppm = ::fixy::in_range<std::uint32_t{0}, kPpmFull>;
inline constexpr auto resource_limit_ppm = ::fixy::in_range<std::uint32_t{1}, kPpmFull>;

using PositiveBackpressureBytes = ::fixy::Positive<std::uint32_t>;
using PositiveConnectionLimit = ::fixy::Positive<std::uint16_t>;
using ResourcePressurePpm = ::fixy::Refined<resource_pressure_ppm, std::uint32_t>;
using ResourceLimitPpm = ::fixy::Refined<resource_limit_ppm, std::uint32_t>;

// Every field is already a checked type, so each request built from them
// is a valid request.
struct ConnectionRequest {
    SocketFd socket;
    PositiveBackpressureBytes initial_credit = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
};

// A pressure sample names a resource from the catalog.  The mint is the
// only way to name one, so no sample carries a kind outside the catalog.
class ResourcePressure {
public:
    constexpr ResourcePressure() noexcept = default;

    [[nodiscard]] constexpr ::foundation::effects::ResourceKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr ResourcePressurePpm used_ppm() const noexcept { return used_ppm_; }

private:
    constexpr ResourcePressure(::foundation::effects::ResourceKind kind, ResourcePressurePpm used_ppm) noexcept
        : kind_{kind}, used_ppm_{used_ppm} {}

    template <::foundation::effects::ResourceKind Kind>
        requires ::foundation::effects::IsResourceKind<Kind>
    friend constexpr std::expected<ResourcePressure, BackpressureError> mint_resource_pressure(std::uint32_t) noexcept;

    ::foundation::effects::ResourceKind kind_ = ::foundation::effects::ResourceKind::NicQ;
    ResourcePressurePpm used_ppm_ = ::fixy::mint_refined<resource_pressure_ppm>(std::uint32_t{0});
};

// A limit names a resource from the catalog, for the same reason.
class ResourceLimit {
public:
    constexpr ResourceLimit() noexcept = default;

    [[nodiscard]] constexpr ::foundation::effects::ResourceKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr ResourceLimitPpm reject_at_or_above_ppm() const noexcept { return reject_at_or_above_ppm_; }

private:
    constexpr ResourceLimit(::foundation::effects::ResourceKind kind, ResourceLimitPpm reject_at_or_above_ppm) noexcept
        : kind_{kind}, reject_at_or_above_ppm_{reject_at_or_above_ppm} {}

    template <::foundation::effects::ResourceKind Kind>
        requires ::foundation::effects::IsResourceKind<Kind>
    friend constexpr std::expected<ResourceLimit, BackpressureError> mint_resource_limit(std::uint32_t) noexcept;

    ::foundation::effects::ResourceKind kind_ = ::foundation::effects::ResourceKind::NicQ;
    ResourceLimitPpm reject_at_or_above_ppm_ = ::fixy::mint_refined<resource_limit_ppm>(std::uint32_t{950000});
};

struct AdmissionDecision {
    AdmissionDecisionKind kind = AdmissionDecisionKind::Accepted;
    SocketFd socket;
    ::foundation::effects::ResourceKind limiting_resource = ::foundation::effects::ResourceKind::NicQ;
    ResourcePressurePpm observed_ppm = ::fixy::mint_refined<resource_pressure_ppm>(std::uint32_t{0});
    ResourceLimitPpm threshold_ppm = ::fixy::mint_refined<resource_limit_ppm>(kPpmFull);
    std::uint32_t retry_after_ms = 0;
    std::uint64_t sequence = 0;
};

using DeclaredAdmissionDecision = ::fixy::Tagged<AdmissionDecision, ::fixy::tags::source::AdmissionDecision>;

namespace detail::backpressure {

// The one admission shape: refuse a value the predicate rejects with the
// error the caller names, and mint the refined value for one it admits.
template <auto Pred, typename T>
[[nodiscard]] constexpr std::expected<::fixy::Refined<Pred, T>, BackpressureError>
admit(T value, BackpressureError refusal) noexcept {
    if (!Pred(value)) {
        return std::unexpected(refusal);
    }
    return ::fixy::mint_refined<Pred>(value);
}

}  // namespace detail::backpressure

[[nodiscard]] constexpr std::expected<PositiveBackpressureBytes, BackpressureError>
admit_backpressure_credit(std::uint32_t bytes) noexcept {
    return detail::backpressure::admit<::fixy::positive>(bytes, BackpressureError::InvalidCreditBytes);
}

[[nodiscard]] constexpr std::expected<PositiveConnectionLimit, BackpressureError>
admit_connection_limit(std::uint16_t limit) noexcept {
    return detail::backpressure::admit<::fixy::positive>(limit, BackpressureError::InvalidConnectionLimit);
}

[[nodiscard]] constexpr std::expected<ResourcePressurePpm, BackpressureError>
admit_resource_pressure_ppm(std::uint32_t ppm) noexcept {
    return detail::backpressure::admit<resource_pressure_ppm>(ppm, BackpressureError::InvalidResourcePressure);
}

[[nodiscard]] constexpr std::expected<ResourceLimitPpm, BackpressureError>
admit_resource_limit_ppm(std::uint32_t ppm) noexcept {
    return detail::backpressure::admit<resource_limit_ppm>(ppm, BackpressureError::InvalidResourceLimit);
}

template <::foundation::effects::ResourceKind Kind>
    requires ::foundation::effects::IsResourceKind<Kind>
[[nodiscard]] constexpr std::expected<ResourcePressure, BackpressureError>
mint_resource_pressure(std::uint32_t used_ppm) noexcept {
    auto admitted = admit_resource_pressure_ppm(used_ppm);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    return ResourcePressure{Kind, *admitted};
}

template <::foundation::effects::ResourceKind Kind>
    requires ::foundation::effects::IsResourceKind<Kind>
[[nodiscard]] constexpr std::expected<ResourceLimit, BackpressureError>
mint_resource_limit(std::uint32_t reject_at_or_above_ppm) noexcept {
    auto admitted = admit_resource_limit_ppm(reject_at_or_above_ppm);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    return ResourceLimit{Kind, *admitted};
}

// The one door from a raw decision to one that may cross a runtime
// boundary.  AdmissionController is its caller.
[[nodiscard]] constexpr DeclaredAdmissionDecision mint_admission_decision(AdmissionDecision decision) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::AdmissionDecision>(decision);
}

[[nodiscard]] constexpr bool resource_pressure_exceeds(ResourcePressure pressure, ResourceLimit limit) noexcept {
    return pressure.kind() == limit.kind() && pressure.used_ppm().value() >= limit.reject_at_or_above_ppm().value();
}

}  // namespace crucible::cntp
