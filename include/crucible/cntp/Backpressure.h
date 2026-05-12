#pragma once

// GAPS-137. CNT-P backpressure/admission-control substrate.
//
// CNT-P owns typed admission facts and admission-decision records. Mutable
// runtime counters live in cntp/BackpressureRuntime.h: credits, live connection
// counts, and latest resource pressure are runtime state, not a transport
// protocol by themselves. This header does not tune sockets, mutate kernel
// receive windows, write Cipher audit events, or invent HealthScorer glue.

#include <crucible/Platform.h>
#include <crucible/cntp/CongestionControl.h>
#include <crucible/effects/Resources.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

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

[[nodiscard]] std::string_view
backpressure_error_name(BackpressureError error) noexcept;
[[nodiscard]] std::string_view
admission_decision_kind_name(AdmissionDecisionKind kind) noexcept;

using PositiveBackpressureBytes = safety::Positive<std::uint32_t>;
using PositiveConnectionLimit = safety::Positive<std::uint16_t>;
using ResourcePressurePpm =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{1'000'000}, std::uint32_t>;
using ResourceLimitPpm =
    safety::Bounded<std::uint32_t{1}, std::uint32_t{1'000'000}, std::uint32_t>;

struct ConnectionRequest {
    SocketFd socket;
    PositiveBackpressureBytes initial_credit{std::uint32_t{1}};
};

struct ResourcePressure {
    effects::ResourceKind kind = effects::ResourceKind::NicQ;
    ResourcePressurePpm used_ppm{std::uint32_t{0}};
};

struct ResourceLimit {
    effects::ResourceKind kind = effects::ResourceKind::NicQ;
    ResourceLimitPpm reject_at_or_above_ppm{std::uint32_t{950'000}};
};

struct AdmissionDecision {
    AdmissionDecisionKind kind = AdmissionDecisionKind::Accepted;
    SocketFd socket;
    effects::ResourceKind limiting_resource = effects::ResourceKind::NicQ;
    ResourcePressurePpm observed_ppm{std::uint32_t{0}};
    ResourceLimitPpm threshold_ppm{std::uint32_t{1'000'000}};
    std::uint32_t retry_after_ms = 0;
    std::uint64_t sequence = 0;
};

using DeclaredAdmissionDecision =
    safety::Tagged<AdmissionDecision, safety::source::AdmissionDecision>;

[[nodiscard]] constexpr std::expected<PositiveBackpressureBytes, BackpressureError>
admit_backpressure_credit(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(BackpressureError::InvalidCreditBytes);
    }
    return PositiveBackpressureBytes{
        bytes, typename PositiveBackpressureBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveConnectionLimit, BackpressureError>
admit_connection_limit(std::uint16_t limit) noexcept {
    if (limit == 0) {
        return std::unexpected(BackpressureError::InvalidConnectionLimit);
    }
    return PositiveConnectionLimit{
        limit, typename PositiveConnectionLimit::Trusted{}};
}

[[nodiscard]] constexpr std::expected<ResourcePressurePpm, BackpressureError>
admit_resource_pressure_ppm(std::uint32_t ppm) noexcept {
    if (ppm > 1'000'000u) {
        return std::unexpected(BackpressureError::InvalidResourcePressure);
    }
    return ResourcePressurePpm{ppm, typename ResourcePressurePpm::Trusted{}};
}

[[nodiscard]] constexpr std::expected<ResourceLimitPpm, BackpressureError>
admit_resource_limit_ppm(std::uint32_t ppm) noexcept {
    if (ppm == 0 || ppm > 1'000'000u) {
        return std::unexpected(BackpressureError::InvalidResourceLimit);
    }
    return ResourceLimitPpm{ppm, typename ResourceLimitPpm::Trusted{}};
}

template <effects::ResourceKind Kind>
    requires effects::IsResourceKind<Kind>
[[nodiscard]] constexpr std::expected<ResourcePressure, BackpressureError>
mint_resource_pressure(std::uint32_t used_ppm) noexcept {
    auto admitted = admit_resource_pressure_ppm(used_ppm);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    return ResourcePressure{
        .kind = Kind,
        .used_ppm = *admitted,
    };
}

template <effects::ResourceKind Kind>
    requires effects::IsResourceKind<Kind>
[[nodiscard]] constexpr std::expected<ResourceLimit, BackpressureError>
mint_resource_limit(std::uint32_t reject_at_or_above_ppm) noexcept {
    auto admitted = admit_resource_limit_ppm(reject_at_or_above_ppm);
    if (!admitted.has_value()) {
        return std::unexpected(admitted.error());
    }
    return ResourceLimit{
        .kind = Kind,
        .reject_at_or_above_ppm = *admitted,
    };
}

[[nodiscard]] constexpr std::expected<ConnectionRequest, BackpressureError>
mint_connection_request(SocketFd socket,
                        PositiveBackpressureBytes initial_credit) noexcept {
    return ConnectionRequest{
        .socket = socket,
        .initial_credit = initial_credit,
    };
}

[[nodiscard]] constexpr DeclaredAdmissionDecision
mint_admission_decision(AdmissionDecision decision) noexcept {
    return DeclaredAdmissionDecision{decision};
}

[[nodiscard]] constexpr bool
resource_pressure_exceeds(ResourcePressure pressure,
                          ResourceLimit limit) noexcept {
    return pressure.kind == limit.kind &&
           pressure.used_ppm.value() >= limit.reject_at_or_above_ppm.value();
}

static_assert(sizeof(PositiveBackpressureBytes) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveConnectionLimit) == sizeof(std::uint16_t));
static_assert(sizeof(ResourcePressurePpm) == sizeof(std::uint32_t));
static_assert(sizeof(ResourceLimitPpm) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredAdmissionDecision) == sizeof(AdmissionDecision));
static_assert(std::is_trivially_copyable_v<ConnectionRequest>);
static_assert(std::is_trivially_copyable_v<ResourcePressure>);
static_assert(std::is_trivially_copyable_v<ResourceLimit>);
static_assert(std::is_trivially_copyable_v<AdmissionDecision>);

}  // namespace crucible::cntp
