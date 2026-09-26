#pragma once

// Applying a config sets socket options only: it mutates no sysctl, installs
// no qdisc and starts no collective runtime.  The ECN and credit-pacing fields
// are declared state for a caller to act on, not settings this header applies.

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>

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
    // A poll result, not a timeout.  A blocking acquire would deadlock: the
    // sole credit consumer for a flow is also the thread that issues it.
    CreditUnavailable,
};

[[nodiscard]] std::string_view incast_error_name(IncastError error) noexcept;

using PositiveCreditBytes = ::fixy::Positive<std::uint32_t>;
using PositiveRtoMinUsec = ::fixy::Positive<std::uint32_t>;
using PositiveSenderCount = ::fixy::Positive<std::uint16_t>;

struct IncastConfig {
    bool enable_dctcp = true;
    bool enable_ecn = true;
    PositiveRtoMinUsec rto_min_usec = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{10000});
    bool enable_credit_pacing = false;
    PositiveCreditBytes initial_credit_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{64 * 1024});
    PositiveSenderCount expected_senders = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1});
};

using DeclaredIncastConfig = ::fixy::Tagged<IncastConfig, ::fixy::tags::source::IncastConfig>;

[[nodiscard]] constexpr std::expected<PositiveCreditBytes, IncastError>
admit_credit_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(IncastError::InvalidCreditBytes);
    }
    return ::fixy::mint_refined<::fixy::positive>(bytes);
}

[[nodiscard]] constexpr std::expected<PositiveRtoMinUsec, IncastError> admit_rto_min_usec(std::uint32_t usec) noexcept {
    if (usec == 0) {
        return std::unexpected(IncastError::InvalidRtoMin);
    }
    return ::fixy::mint_refined<::fixy::positive>(usec);
}

[[nodiscard]] constexpr std::expected<PositiveSenderCount, IncastError>
admit_sender_count(std::uint16_t senders) noexcept {
    if (senders == 0) {
        return std::unexpected(IncastError::InvalidSenderCount);
    }
    return ::fixy::mint_refined<::fixy::positive>(senders);
}

[[nodiscard]] constexpr std::expected<DeclaredIncastConfig, IncastError>
mint_incast_config(IncastConfig config) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::IncastConfig>(config);
}

template <LinkClass Link>
    requires(Link == LinkClass::LosslessDatacenterFabric)
[[nodiscard]] constexpr DeclaredIncastConfig mint_dctcp_incast_config(
    PositiveCreditBytes initial_credit = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{64 * 1024}),
    PositiveRtoMinUsec rto_min = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{10000}),
    PositiveSenderCount senders = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{1})) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::IncastConfig>(IncastConfig{
        .enable_dctcp = true,
        .enable_ecn = true,
        .rto_min_usec = rto_min,
        .enable_credit_pacing = true,
        .initial_credit_bytes = initial_credit,
        .expected_senders = senders,
    });
}

[[nodiscard]] std::expected<void, IncastError> set_socket_rto_min_usec(SocketFd fd,
                                                                       PositiveRtoMinUsec rto_min) noexcept;

// A config that enables DCTCP first asks the kernel whether it has DCTCP,
// and that question is a read under /proc, so the context must carry what
// the fs door demands for it.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] std::expected<void, IncastError> apply_incast_config(Ctx const& ctx, SocketFd fd,
                                                                   DeclaredIncastConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_dctcp) {
        if (!kernel_supports(ctx, CcAlgorithm::Dctcp)) {
            return std::unexpected(IncastError::AlgorithmUnavailable);
        }
        auto choice = mint_cc_choice<CcAlgorithm::Dctcp, LinkClass::LosslessDatacenterFabric>();
        auto set = set_cc_for_socket(ctx, fd, choice);
        if (!set.has_value()) {
            return std::unexpected(IncastError::SetCcFailed);
        }
    }

    auto rto = set_socket_rto_min_usec(fd, raw.rto_min_usec);
    if (!rto.has_value()) {
        return std::unexpected(rto.error());
    }
    return {};
}

static_assert(sizeof(PositiveCreditBytes) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveRtoMinUsec) == sizeof(std::uint32_t));
static_assert(sizeof(PositiveSenderCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredIncastConfig) == sizeof(IncastConfig));
// A refined field keeps the config from being trivially copyable, so no
// byte copy builds one past the predicates.  The copy and the destructor
// stay trivial, so the config still passes as a plain value.
static_assert(std::is_trivially_copy_constructible_v<IncastConfig> && std::is_trivially_destructible_v<IncastConfig>);

}  // namespace crucible::cntp
