#include <crucible/cntp/RoceConfig.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

namespace crucible::cntp {

namespace {

class LocalFd {
public:
    explicit LocalFd(int fd) noexcept : fd_{fd} {}
    LocalFd(LocalFd const&) = delete;
    LocalFd& operator=(LocalFd const&) = delete;
    LocalFd(LocalFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    LocalFd& operator=(LocalFd&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~LocalFd() noexcept { close(); }

    [[nodiscard]] int raw() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

[[nodiscard]] std::expected<std::uint64_t, RoceError>
parse_u64(std::string_view text) noexcept {
    std::uint64_t value = 0;
    std::size_t pos = 0;
    while (pos < text.size() && is_space(text[pos])) {
        ++pos;
    }
    const std::size_t first_digit = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(text[pos] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return std::unexpected(RoceError::CounterParseFailed);
        }
        value = value * 10u + digit;
        ++pos;
    }
    if (pos == first_digit) {
        return std::unexpected(RoceError::CounterParseFailed);
    }
    while (pos < text.size()) {
        if (!is_space(text[pos])) {
            return std::unexpected(RoceError::CounterParseFailed);
        }
        ++pos;
    }
    return value;
}

[[nodiscard]] std::expected<std::uint64_t, RoceError>
read_counter_file(char const* path) noexcept {
    std::array<char, 64> bytes{};
    LocalFd fd{::open(path, O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) {
        static_cast<void>(errno);
        return std::unexpected(RoceError::CounterUnavailable);
    }
    const auto nread = ::read(fd.raw(), bytes.data(), bytes.size() - 1u);
    if (nread <= 0) {
        static_cast<void>(errno);
        return std::unexpected(RoceError::CounterUnavailable);
    }
    return parse_u64(std::string_view{
        bytes.data(), static_cast<std::size_t>(nread)});
}

[[nodiscard]] std::array<char, 96>
stat_path(NicInterfaceName iface, std::string_view leaf) noexcept {
    std::array<char, 96> out{};
    constexpr std::string_view prefix = "/sys/class/net/";
    constexpr std::string_view mid = "/statistics/";
    static_assert(prefix.size() + NicInterfaceName::max_bytes + mid.size() +
                      std::string_view{"rx_pause_frames"}.size() + 1u <=
                  out.size());
    static_assert(prefix.size() + NicInterfaceName::max_bytes + mid.size() +
                      std::string_view{"tx_pause_frames"}.size() + 1u <=
                  out.size());
    std::size_t pos = 0;
    auto append = [&](std::string_view chunk) noexcept {
        for (char c : chunk) {
            if (pos + 1u < out.size()) {
                out[pos++] = c;
            }
        }
    };
    append(prefix);
    append(iface.view());
    append(mid);
    append(leaf);
    out[pos] = '\0';
    return out;
}

}  // namespace

std::string_view roce_error_name(RoceError error) noexcept {
    switch (error) {
        case RoceError::InvalidPfcPriorityMask:  return "InvalidPfcPriorityMask";
        case RoceError::InvalidDscp:             return "InvalidDscp";
        case RoceError::InvalidDcqcnAlpha:       return "InvalidDcqcnAlpha";
        case RoceError::InvalidDcqcnTargetPackets:
            return "InvalidDcqcnTargetPackets";
        case RoceError::InvalidCeThresholdBytes: return "InvalidCeThresholdBytes";
        case RoceError::CounterUnavailable:      return "CounterUnavailable";
        case RoceError::CounterParseFailed:      return "CounterParseFailed";
        case RoceError::PrivilegedApplyDeferred: return "PrivilegedApplyDeferred";
        case RoceError::VendorBackendUnavailable:
            return "VendorBackendUnavailable";
        case RoceError::DcqcnStatusUnavailable:  return "DcqcnStatusUnavailable";
        default:                                 return "<unknown RoceError>";
    }
}

std::expected<void, RoceError>
apply_roce_config(DeclaredRoceConfig config) noexcept {
    auto valid = validate_roce_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!config.value().allow_privileged_apply) {
        return std::unexpected(RoceError::PrivilegedApplyDeferred);
    }
    return std::unexpected(RoceError::VendorBackendUnavailable);
}

std::expected<PfcPauseStats, RoceError>
parse_pfc_pause_counters(std::string_view rx_text,
                         std::string_view tx_text) noexcept {
    auto rx = parse_u64(rx_text);
    if (!rx.has_value()) {
        return std::unexpected(rx.error());
    }
    auto tx = parse_u64(tx_text);
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }
    return PfcPauseStats{
        .rx_pause_frames = *rx,
        .tx_pause_frames = *tx,
    };
}

std::expected<PfcPauseStats, RoceError>
query_pfc_pause_counters(NicInterfaceName iface) noexcept {
    const auto rx_path = stat_path(iface, "rx_pause_frames");
    const auto tx_path = stat_path(iface, "tx_pause_frames");
    auto rx = read_counter_file(rx_path.data());
    if (!rx.has_value()) {
        return std::unexpected(rx.error());
    }
    auto tx = read_counter_file(tx_path.data());
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }
    return PfcPauseStats{
        .rx_pause_frames = *rx,
        .tx_pause_frames = *tx,
    };
}

std::string_view dcqcn_state_name(DcqcnState state) noexcept {
    switch (state) {
        case DcqcnState::BackendUnavailable: return "BackendUnavailable";
        case DcqcnState::Inactive:           return "Inactive";
        case DcqcnState::Active:             return "Active";
        default:                             return "<unknown DcqcnState>";
    }
}

DcqcnState query_dcqcn_state(NicInterfaceName iface) noexcept {
    // fixy-A5-042: vendor-specific probes (Mellanox `/sys/class/
    // infiniband/<dev>/tc/<n>/cnp_dscp`, etc.) land here as they
    // ship.  Until a backend is wired, the honest answer is
    // BackendUnavailable — NOT a fabricated Inactive that pretends
    // the NIC reported off.
    static_cast<void>(iface);
    return DcqcnState::BackendUnavailable;
}

std::expected<bool, RoceError>
verify_dcqcn_active(NicInterfaceName iface) noexcept {
    switch (query_dcqcn_state(iface)) {
        case DcqcnState::Active:   return true;
        case DcqcnState::Inactive: return false;
        case DcqcnState::BackendUnavailable:
        default:
            return std::unexpected(RoceError::DcqcnStatusUnavailable);
    }
}

}  // namespace crucible::cntp
