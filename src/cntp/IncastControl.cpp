#include <crucible/cntp/IncastControl.h>

#include <cerrno>

#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace crucible::cntp {

std::string_view incast_error_name(IncastError error) noexcept {
    switch (error) {
        case IncastError::InvalidSocketFd:          return "InvalidSocketFd";
        case IncastError::InvalidCreditBytes:       return "InvalidCreditBytes";
        case IncastError::InvalidRtoMin:            return "InvalidRtoMin";
        case IncastError::InvalidSenderCount:       return "InvalidSenderCount";
        case IncastError::AlgorithmUnavailable:     return "AlgorithmUnavailable";
        case IncastError::SetCcFailed:              return "SetCcFailed";
        case IncastError::SetRtoMinFailed:          return "SetRtoMinFailed";
        case IncastError::UnsupportedRtoMinSockOpt: return "UnsupportedRtoMinSockOpt";
        case IncastError::TooManyFlows:             return "TooManyFlows";
        case IncastError::FlowNotStarted:           return "FlowNotStarted";
        case IncastError::CreditOverflow:           return "CreditOverflow";
        case IncastError::CreditUnavailable:        return "CreditUnavailable";
        default:                                    return "<unknown IncastError>";
    }
}

std::expected<void, IncastError>
set_socket_rto_min_usec(SocketFd fd, PositiveRtoMinUsec rto_min) noexcept {
#ifdef TCP_RTO_MIN_US
    const std::uint32_t value = rto_min.value();
    const int rc = ::setsockopt(
        fd.value(),
        IPPROTO_TCP,
        TCP_RTO_MIN_US,
        &value,
        static_cast<socklen_t>(sizeof(value)));
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(IncastError::SetRtoMinFailed);
    }
    return {};
#else
    static_cast<void>(fd);
    static_cast<void>(rto_min);
    return std::unexpected(IncastError::UnsupportedRtoMinSockOpt);
#endif
}

std::expected<void, IncastError>
apply_incast_config(SocketFd fd, DeclaredIncastConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_dctcp) {
        if (!kernel_supports(CcAlgorithm::Dctcp)) {
            return std::unexpected(IncastError::AlgorithmUnavailable);
        }
        auto choice =
            mint_cc_choice<CcAlgorithm::Dctcp, LinkClass::LosslessDatacenterFabric>();
        auto set = set_cc_for_socket(fd, choice);
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

}  // namespace crucible::cntp
