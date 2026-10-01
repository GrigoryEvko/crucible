#pragma once

// BBR paces from its own rate estimate, so the throughput claim holds only
// when the root qdisc paces as well.  That is why only fq and fq_codel count
// as compatible.
//
// Nothing here installs a qdisc.  ensure_fq_active reads the live one and
// reports whether it matches.  allow_auto_config only selects which error
// comes back, and the fq parameters are declared values that nothing applies.

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Socket.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

enum class Qdisc : std::uint8_t {
    Fq = 0,
    FqCodel = 1,
    Pfifo = 2,
    Mq = 3,
    Noqueue = 4,
    Other = 5,
};

enum class PacingError : std::uint8_t {
    InvalidInterfaceName,
    InvalidSocketFd,
    InvalidPacingRate,
    NetlinkOpenFailed,
    NetlinkSendFailed,
    NetlinkReceiveFailed,
    InterfaceNotFound,
    QdiscKindMissing,
    FqRequired,
    AutoConfigDeferred,
    SetSockOptFailed,
};

[[nodiscard]] std::string_view qdisc_name(Qdisc qdisc) noexcept;

using PositivePacingRate = ::fixy::Positive<std::uint64_t>;
using PositiveFqParam = ::fixy::Positive<std::uint32_t>;

// The stored length is private and from() is the only code that writes
// it, so every NicInterfaceName that exists satisfies
// view().size() < max_bytes.  That is a construction guarantee, not a
// claim about the caller: consumers copy view() into a kernel
// char[IFNAMSIZ] field and write the terminating NUL at view().size(),
// and the invariant is what keeps that store in range.
//
// The class deliberately is not an aggregate.  While bytes and size
// were public members, `NicInterfaceName n{}; n.size = 200;` was
// well-formed and reached that kernel field, so each consumer had to
// re-check a bound the type already claimed to hold.
class NicInterfaceName {
public:
    static constexpr std::size_t max_bytes = 16;

    // A default-constructed name is empty.  Consumers read that as "no
    // interface selected"; it copies zero bytes and stores the NUL at
    // index 0, which is in range for every buffer of max_bytes or more.
    constexpr NicInterfaceName() noexcept = default;

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {bytes_.data(), size_}; }

    [[nodiscard]] static constexpr std::expected<NicInterfaceName, PacingError> from(std::string_view name) noexcept {
        if (name.empty() || name.size() >= max_bytes) {
            return std::unexpected(PacingError::InvalidInterfaceName);
        }

        NicInterfaceName out{};
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                         || c == '-' || c == '.' || c == ':';
            if (!ok) {
                return std::unexpected(PacingError::InvalidInterfaceName);
            }
            out.bytes_[i] = c;
        }
        out.size_ = static_cast<std::uint8_t>(name.size());
        return out;
    }

private:
    std::array<char, max_bytes> bytes_{};
    std::uint8_t size_ = 0;
};

struct FqConfig {
    PositiveFqParam max_quantum = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{8192});
    PositiveFqParam flow_limit = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{100});
    PositiveFqParam low_rate_threshold_kbps = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{50});
};

struct QdiscConfig {
    NicInterfaceName interface{};
    Qdisc required = Qdisc::Fq;
    FqConfig fq{};
    bool allow_auto_config = false;
};

using DeclaredQdiscConfig = ::fixy::Tagged<QdiscConfig, ::fixy::tags::source::QdiscConfig>;

template <Qdisc Q>
concept BbrCompatibleQdisc = Q == Qdisc::Fq || Q == Qdisc::FqCodel;

[[nodiscard]] constexpr std::expected<PositivePacingRate, PacingError>
admit_pacing_rate(std::uint64_t bytes_per_second) noexcept {
    if (bytes_per_second == 0) {
        return std::unexpected(PacingError::InvalidPacingRate);
    }
    return ::fixy::mint_refined<::fixy::positive>(bytes_per_second);
}

template <Qdisc Required>
    requires BbrCompatibleQdisc<Required>
[[nodiscard]] constexpr DeclaredQdiscConfig mint_bbr_qdisc_config(NicInterfaceName iface, FqConfig fq = {},
                                                                  bool allow_auto_config = false) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::QdiscConfig>(QdiscConfig{
        .interface = iface,
        .required = Required,
        .fq = fq,
        .allow_auto_config = allow_auto_config,
    });
}

[[nodiscard]] std::expected<Qdisc, PacingError> qdisc_from_kernel_name(std::string_view name) noexcept;

[[nodiscard]] std::expected<Qdisc, PacingError> parse_tc_qdisc_show(std::string_view text) noexcept;

namespace detail {

// Runs the qdisc dump over a netlink socket that the socket door opened.
// The descriptor is borrowed; the caller's handle closes it.
[[nodiscard]] std::expected<Qdisc, PacingError> query_active_qdisc_over(::fixy::fs::OwnedFd const& nl,
                                                                        NicInterfaceName iface) noexcept;

}  // namespace detail

// The query opens a netlink socket through the socket door, which counts
// the socket family as IO and Block, so only a context that carries both
// may ask which qdisc is live.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketMint<Ctx, ::fixy::net::socket_kind::NetlinkRoute>
[[nodiscard]] std::expected<Qdisc, PacingError> query_active_qdisc(Ctx const& ctx, NicInterfaceName iface) noexcept {
    auto opened = ::fixy::net::mint_socket<::fixy::net::socket_kind::NetlinkRoute>(ctx);
    if (!opened.has_value()) {
        return std::unexpected(PacingError::NetlinkOpenFailed);
    }
    const ::fixy::fs::OwnedFd nl = std::move(*opened).consume();
    return detail::query_active_qdisc_over(nl, iface);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketMint<Ctx, ::fixy::net::socket_kind::NetlinkRoute>
[[nodiscard]] std::expected<void, PacingError> ensure_fq_active(Ctx const& ctx, DeclaredQdiscConfig config) noexcept {
    auto const& raw = config.value();
    auto active = query_active_qdisc(ctx, raw.interface);
    if (!active.has_value()) {
        return std::unexpected(active.error());
    }
    if (*active == Qdisc::Fq || *active == Qdisc::FqCodel) {
        return {};
    }
    if (raw.allow_auto_config) {
        return std::unexpected(PacingError::AutoConfigDeferred);
    }
    return std::unexpected(PacingError::FqRequired);
}

namespace detail {

// The SO_MAX_PACING_RATE body.  It takes the key, so only the gated form
// below reaches setsockopt.
[[nodiscard]] std::expected<void, PacingError>
set_socket_pacing_rate_keyed(SocketOptionKey const&, SocketFd fd, PositivePacingRate bytes_per_second) noexcept;

}  // namespace detail

// The rate is a socket option, so the call takes a context that admits
// the socket option row.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<void, PacingError> set_socket_pacing_rate(Ctx const& ctx, SocketFd fd,
                                                                      PositivePacingRate bytes_per_second) noexcept {
    return detail::set_socket_pacing_rate_keyed(detail::socket_option_key_(ctx), fd, bytes_per_second);
}

}  // namespace crucible::cntp
