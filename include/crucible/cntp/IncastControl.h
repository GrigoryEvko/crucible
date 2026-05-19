#pragma once

// GAPS-124.  CNT-P incast-control substrate.
//
// This header owns socket-local incast mitigations that can be applied
// without inventing a collective runtime: DCTCP selection, TCP_RTO_MIN_US
// tuning where the kernel exposes it, and declared receiver-issued credit
// pacing config. Bounded credit state lives in cntp/IncastControlRuntime.h. This
// header deliberately does not mutate sysctls, install qdiscs, write Cipher
// events, or auto-wire collectives; those are owned by NicConfig, Pacing,
// Cipher, and CollectiveCatalog tasks.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class IncastError : std::uint8_t {
    InvalidSocketFd,
    InvalidCreditBytes,
    InvalidRtoMin,
    InvalidSenderCount,
    AlgorithmUnavailable,
    SetCcFailed,
    SetRtoMinFailed,
    UnsupportedRtoMinSockOpt,
    TooManyFlows,
    FlowNotStarted,
    CreditOverflow,
    // fixy-A5-005: this error means "no credit currently issued for this
    // flow" — try_consume_credit polls once and returns immediately.  It
    // is NOT a timeout (the controller never blocks); the previous name
    // CreditTimeout falsely implied a wait that the design rejects.  The
    // BG drain thread is the sole credit consumer per flow, so a wait
    // would be a single-threaded self-deadlock.
    CreditUnavailable,
};

[[nodiscard]] std::string_view incast_error_name(IncastError error) noexcept;

using PositiveCreditBytes = safety::Positive<std::uint32_t>;
using PositiveRtoMinUsec = safety::Positive<std::uint32_t>;
using PositiveSenderCount = safety::Positive<std::uint16_t>;

struct IncastConfig {
    bool enable_dctcp = true;
    bool enable_ecn = true;
    PositiveRtoMinUsec rto_min_usec{std::uint32_t{10'000}};
    bool enable_credit_pacing = false;
    PositiveCreditBytes initial_credit_bytes{std::uint32_t{64 * 1024}};
    PositiveSenderCount expected_senders{std::uint16_t{1}};
};

using DeclaredIncastConfig =
    safety::Tagged<IncastConfig, safety::source::IncastConfig>;

[[nodiscard]] constexpr std::expected<PositiveCreditBytes, IncastError>
admit_credit_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(IncastError::InvalidCreditBytes);
    }
    return PositiveCreditBytes{bytes, typename PositiveCreditBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveRtoMinUsec, IncastError>
admit_rto_min_usec(std::uint32_t usec) noexcept {
    if (usec == 0) {
        return std::unexpected(IncastError::InvalidRtoMin);
    }
    return PositiveRtoMinUsec{usec, typename PositiveRtoMinUsec::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveSenderCount, IncastError>
admit_sender_count(std::uint16_t senders) noexcept {
    if (senders == 0) {
        return std::unexpected(IncastError::InvalidSenderCount);
    }
    return PositiveSenderCount{senders, typename PositiveSenderCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DeclaredIncastConfig, IncastError>
mint_incast_config(IncastConfig config) noexcept {
    return DeclaredIncastConfig{config};
}

template <LinkClass Link>
    requires (Link == LinkClass::LosslessDatacenterFabric)
[[nodiscard]] constexpr DeclaredIncastConfig
mint_dctcp_incast_config(PositiveCreditBytes initial_credit =
                             PositiveCreditBytes{std::uint32_t{64 * 1024}},
                         PositiveRtoMinUsec rto_min =
                             PositiveRtoMinUsec{std::uint32_t{10'000}},
                         PositiveSenderCount senders =
                             PositiveSenderCount{std::uint16_t{1}}) noexcept {
    return DeclaredIncastConfig{IncastConfig{
        .enable_dctcp = true,
        .enable_ecn = true,
        .rto_min_usec = rto_min,
        .enable_credit_pacing = true,
        .initial_credit_bytes = initial_credit,
        .expected_senders = senders,
    }};
}

[[nodiscard]] std::expected<void, IncastError>
set_socket_rto_min_usec(SocketFd fd, PositiveRtoMinUsec rto_min) noexcept;

[[nodiscard]] std::expected<void, IncastError>
apply_incast_config(SocketFd fd, DeclaredIncastConfig config) noexcept;

static_assert(sizeof(PositiveCreditBytes) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveRtoMinUsec) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveSenderCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredIncastConfig) == sizeof(IncastConfig));
static_assert(std::is_trivially_copyable_v<IncastConfig>);

}  // namespace crucible::cntp
