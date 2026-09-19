#pragma once

// BBR paces from its own rate estimate, so the throughput claim holds only
// when the root qdisc paces as well.  That is why only fq and fq_codel count
// as compatible.
//
// Nothing here installs a qdisc.  ensure_fq_active reads the live one and
// reports whether it matches.  allow_auto_config only selects which error
// comes back, and the fq parameters are declared values that nothing applies.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/safety/_Refined.h>
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
    PositiveFqParam max_quantum{8192};
    PositiveFqParam flow_limit{100};
    PositiveFqParam low_rate_threshold_kbps{50};
};

struct QdiscConfig {
    NicInterfaceName interface{};
    Qdisc required = Qdisc::Fq;
    FqConfig fq{};
    bool allow_auto_config = false;
};

using DeclaredQdiscConfig = safety::Tagged<QdiscConfig, safety::source::QdiscConfig>;

static_assert(sizeof(PositivePacingRate) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<NicInterfaceName>);
static_assert(std::is_trivially_copyable_v<QdiscConfig>);
// Aggregate initialization would reach the private length again, this
// time through `NicInterfaceName{bytes, 200}` rather than assignment.
// from() must stay the only writer of the length.
static_assert(!std::is_aggregate_v<NicInterfaceName>,
              "NicInterfaceName must not be an aggregate: from() is the only path that may set the length");

template <Qdisc Q>
concept BbrCompatibleQdisc = Q == Qdisc::Fq || Q == Qdisc::FqCodel;

[[nodiscard]] constexpr std::expected<PositivePacingRate, PacingError>
admit_pacing_rate(std::uint64_t bytes_per_second) noexcept {
    if (bytes_per_second == 0) {
        return std::unexpected(PacingError::InvalidPacingRate);
    }
    return PositivePacingRate{bytes_per_second, typename PositivePacingRate::Trusted{}};
}

template <Qdisc Required>
    requires BbrCompatibleQdisc<Required>
[[nodiscard]] constexpr DeclaredQdiscConfig mint_bbr_qdisc_config(NicInterfaceName iface, FqConfig fq = {},
                                                                  bool allow_auto_config = false) noexcept {
    return DeclaredQdiscConfig{QdiscConfig{
        .interface = iface,
        .required = Required,
        .fq = fq,
        .allow_auto_config = allow_auto_config,
    }};
}

[[nodiscard]] std::expected<Qdisc, PacingError> qdisc_from_kernel_name(std::string_view name) noexcept;

[[nodiscard]] std::expected<Qdisc, PacingError> parse_tc_qdisc_show(std::string_view text) noexcept;

[[nodiscard]] std::expected<Qdisc, PacingError> query_active_qdisc(NicInterfaceName iface) noexcept;

[[nodiscard]] std::expected<void, PacingError> ensure_fq_active(DeclaredQdiscConfig config) noexcept;

[[nodiscard]] std::expected<void, PacingError> set_socket_pacing_rate(SocketFd fd,
                                                                      PositivePacingRate bytes_per_second) noexcept;

}  // namespace crucible::cntp
