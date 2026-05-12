#pragma once

// CNT-P socket pacing + fq/qdisc verification.
//
// GAPS-121 pairs with CongestionControl.h: BBR selections are only
// throughput-sound when the NIC root qdisc is fq or fq_codel. This
// header owns typed qdisc classification, bounded fq config, live
// qdisc query, and SO_MAX_PACING_RATE. Privileged qdisc replacement is
// intentionally deferred to the NicConfig operator-policy task.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

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

using PositivePacingRate = safety::Positive<std::uint64_t>;
using PositiveFqParam = safety::Positive<std::uint32_t>;

struct NicInterfaceName {
    static constexpr std::size_t max_bytes = 16;

    std::array<char, max_bytes> bytes{};
    std::uint8_t size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {bytes.data(), size};
    }

    [[nodiscard]] static constexpr std::expected<NicInterfaceName, PacingError>
    from(std::string_view name) noexcept {
        if (name.empty() || name.size() >= max_bytes) {
            return std::unexpected(PacingError::InvalidInterfaceName);
        }

        NicInterfaceName out{};
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' ||
                c == '-' ||
                c == '.' ||
                c == ':';
            if (!ok) {
                return std::unexpected(PacingError::InvalidInterfaceName);
            }
            out.bytes[i] = c;
        }
        out.size = static_cast<std::uint8_t>(name.size());
        return out;
    }
};

struct FqConfig {
    PositiveFqParam max_quantum{8'192};
    PositiveFqParam flow_limit{100};
    PositiveFqParam low_rate_threshold_kbps{50};
};

struct QdiscConfig {
    NicInterfaceName interface{};
    Qdisc required = Qdisc::Fq;
    FqConfig fq{};
    bool allow_auto_config = false;
};

using DeclaredQdiscConfig =
    safety::Tagged<QdiscConfig, safety::source::QdiscConfig>;

static_assert(sizeof(PositivePacingRate) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<NicInterfaceName>);
static_assert(std::is_trivially_copyable_v<QdiscConfig>);

template <Qdisc Q>
concept BbrCompatibleQdisc = Q == Qdisc::Fq || Q == Qdisc::FqCodel;

[[nodiscard]] constexpr std::expected<PositivePacingRate, PacingError>
admit_pacing_rate(std::uint64_t bytes_per_second) noexcept {
    if (bytes_per_second == 0) {
        return std::unexpected(PacingError::InvalidPacingRate);
    }
    return PositivePacingRate{
        bytes_per_second, typename PositivePacingRate::Trusted{}};
}

template <Qdisc Required>
    requires BbrCompatibleQdisc<Required>
[[nodiscard]] constexpr DeclaredQdiscConfig
mint_bbr_qdisc_config(NicInterfaceName iface,
                      FqConfig fq = {},
                      bool allow_auto_config = false) noexcept {
    return DeclaredQdiscConfig{QdiscConfig{
        .interface = iface,
        .required = Required,
        .fq = fq,
        .allow_auto_config = allow_auto_config,
    }};
}

[[nodiscard]] std::expected<Qdisc, PacingError>
qdisc_from_kernel_name(std::string_view name) noexcept;

[[nodiscard]] std::expected<Qdisc, PacingError>
parse_tc_qdisc_show(std::string_view text) noexcept;

[[nodiscard]] std::expected<Qdisc, PacingError>
query_active_qdisc(NicInterfaceName iface) noexcept;

[[nodiscard]] std::expected<void, PacingError>
ensure_fq_active(DeclaredQdiscConfig config) noexcept;

[[nodiscard]] std::expected<void, PacingError>
set_socket_pacing_rate(SocketFd fd, PositivePacingRate bytes_per_second) noexcept;

}  // namespace crucible::cntp
