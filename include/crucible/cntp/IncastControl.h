#pragma once

// Applying a config sets socket options only: it mutates no sysctl, installs
// no qdisc and starts no collective runtime.  The ECN and credit-pacing fields
// are declared state for a caller to act on, not settings this header applies.

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Socket.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstdint>
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

[[nodiscard]] ::fixy::TextView incast_error_name(IncastError error) noexcept;

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

[[nodiscard]] constexpr ::fixy::Result<PositiveCreditBytes, IncastError>
admit_credit_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return ::fixy::err(IncastError::InvalidCreditBytes);
    }
    return ::fixy::mint_refined<::fixy::positive>(bytes);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveRtoMinUsec, IncastError>
admit_rto_min_usec(std::uint32_t usec) noexcept {
    if (usec == 0) {
        return ::fixy::err(IncastError::InvalidRtoMin);
    }
    return ::fixy::mint_refined<::fixy::positive>(usec);
}

[[nodiscard]] constexpr ::fixy::Result<PositiveSenderCount, IncastError>
admit_sender_count(std::uint16_t senders) noexcept {
    if (senders == 0) {
        return ::fixy::err(IncastError::InvalidSenderCount);
    }
    return ::fixy::mint_refined<::fixy::positive>(senders);
}

[[nodiscard]] constexpr ::fixy::Result<DeclaredIncastConfig, IncastError>
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

// The minimum retransmit timeout is a socket option (TCP_RTO_MIN_US), so the
// call takes a context that admits the socket option row.  A kernel with no
// such option gives UnsupportedRtoMinSockOpt.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, IncastError> set_socket_rto_min_usec(Ctx const& ctx, SocketFd fd,
                                                                                PositiveRtoMinUsec rto_min) noexcept {
    using Set = ::fixy::Result<::fixy::Unit, IncastError>;
    return ::fixy::net::set_tcp_rto_min(ctx, fd, rto_min.value())
        .match([](::fixy::Unit) -> Set { return ::fixy::Unit{}; },
               [](::foundation::os::Errno error) -> Set {
                   bool const is_unsupported = error == ::foundation::os::Errno::enoprotoopt;
                   return ::fixy::err(is_unsupported ? IncastError::UnsupportedRtoMinSockOpt
                                                     : IncastError::SetRtoMinFailed);
               });
}

// A config that enables DCTCP first asks the kernel whether it has DCTCP.
// That question is a read under /proc, so the context must carry what the
// fs door demands for it.  The config then sets socket options, so the
// context must also admit the socket option row.
template <class Ctx>
concept CtxFitsIncastApply = ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode> && CtxFitsSocketOption<Ctx>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsIncastApply<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, IncastError> apply_incast_config(Ctx const& ctx, SocketFd fd,
                                                                            DeclaredIncastConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_dctcp) {
        if (!kernel_supports(ctx, CcAlgorithm::Dctcp)) {
            return ::fixy::err(IncastError::AlgorithmUnavailable);
        }
        auto choice = mint_cc_choice<CcAlgorithm::Dctcp, LinkClass::LosslessDatacenterFabric>();
        if (set_cc_for_socket(ctx, fd, choice).is_err()) {
            return ::fixy::err(IncastError::SetCcFailed);
        }
    }
    return set_socket_rto_min_usec(ctx, fd, raw.rto_min_usec);
}

}  // namespace crucible::cntp
