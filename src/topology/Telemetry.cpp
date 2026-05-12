#include <crucible/topology/Telemetry.h>

#include <charconv>
#include <system_error>
#include <string_view>
#include <utility>

namespace crucible::topology {

namespace {

[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                         s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                         s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] constexpr std::pair<std::string_view, std::string_view>
split_key_value(std::string_view line) noexcept {
    const std::size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
        return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
    }
    const std::size_t eq = line.find('=');
    if (eq != std::string_view::npos) {
        return {trim(line.substr(0, eq)), trim(line.substr(eq + 1))};
    }
    const std::size_t space = line.find(' ');
    if (space != std::string_view::npos) {
        return {trim(line.substr(0, space)), trim(line.substr(space + 1))};
    }
    return {trim(line), std::string_view{}};
}

[[nodiscard]] constexpr bool contains_ci(std::string_view haystack,
                                         std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    auto lower = [](char c) constexpr noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr std::string_view first_token(std::string_view s) noexcept {
    s = trim(s);
    const std::size_t end = s.find_first_of(" \t\r\n");
    return end == std::string_view::npos ? s : s.substr(0, end);
}

[[nodiscard]] constexpr bool parse_u64(std::string_view value,
                                       std::uint64_t& out) noexcept {
    value = first_token(value);
    if (value.empty()) {
        return false;
    }
    auto const* first = value.data();
    auto const* last = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

[[nodiscard]] constexpr bool parse_leading_u64(std::string_view value,
                                               std::uint64_t& out) noexcept {
    value = first_token(value);
    if (value.empty() || value.front() < '0' || value.front() > '9') {
        return false;
    }
    std::size_t digits = 0;
    while (digits < value.size() && value[digits] >= '0' && value[digits] <= '9') {
        ++digits;
    }
    auto const* first = value.data();
    auto const* last = value.data() + digits;
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

[[nodiscard]] constexpr bool parse_u32(std::string_view value,
                                       std::uint32_t& out) noexcept {
    std::uint64_t tmp = 0;
    if (!parse_u64(value, tmp) || tmp > UINT32_MAX) {
        return false;
    }
    out = static_cast<std::uint32_t>(tmp);
    return true;
}

[[nodiscard]] constexpr std::uint32_t count_packets_suffix(std::string_view value) noexcept {
    value = trim(value);
    while (!value.empty()) {
        value = trim(value);
        const std::size_t end = value.find_first_of(" \t\r\n");
        const std::string_view token = end == std::string_view::npos
            ? value
            : value.substr(0, end);
        if (token.size() > 1 && token.back() == 'p') {
            std::uint64_t parsed = 0;
            if (parse_leading_u64(token, parsed) && parsed <= UINT32_MAX) {
                return static_cast<std::uint32_t>(parsed);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        value.remove_prefix(end + 1);
    }
    return 0;
}

}  // namespace

std::string_view nic_telemetry_error_name(NicTelemetryError error) noexcept {
    switch (error) {
        case NicTelemetryError::None:                 return "None";
        case NicTelemetryError::EmptyInput:           return "EmptyInput";
        case NicTelemetryError::MalformedRecord:      return "MalformedRecord";
        case NicTelemetryError::MissingRequiredField: return "MissingRequiredField";
        case NicTelemetryError::InvalidNicCog:        return "InvalidNicCog";
        case NicTelemetryError::EmptyHistory:         return "EmptyHistory";
        case NicTelemetryError::InvalidWindow:        return "InvalidWindow";
        case NicTelemetryError::NonPositiveCapacity:  return "NonPositiveCapacity";
        default:                                      return "<unknown NicTelemetryError>";
    }
}

std::expected<DeclaredNetdevCounters, NicTelemetryError>
parse_netdev_counters(ExternalTelemetryText text) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(NicTelemetryError::EmptyInput);
    }
    NetdevCounters counters{};
    std::uint16_t admitted = 0;
    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        auto [key, value] = split_key_value(line);
        std::uint64_t parsed = 0;
        if (!parse_u64(value, parsed)) {
            continue;
        }
        if (key == "rx_bytes") {
            counters.rx_bytes = parsed;
        } else if (key == "tx_bytes") {
            counters.tx_bytes = parsed;
        } else if (key == "rx_packets") {
            counters.rx_packets = parsed;
        } else if (key == "tx_packets") {
            counters.tx_packets = parsed;
        } else if (key == "rx_dropped") {
            counters.rx_dropped = parsed;
        } else if (key == "tx_dropped") {
            counters.tx_dropped = parsed;
        } else if (key == "rx_errors") {
            counters.rx_errors = parsed;
        } else if (key == "tx_errors") {
            counters.tx_errors = parsed;
        } else if (key == "rx_fifo_errors") {
            counters.rx_fifo_errors = parsed;
        } else if (key == "tx_fifo_errors") {
            counters.tx_fifo_errors = parsed;
        } else {
            continue;
        }
        ++admitted;
    }
    if (admitted == 0) {
        return std::unexpected(NicTelemetryError::MissingRequiredField);
    }
    return declare_netdev_counters(counters);
}

std::expected<DeclaredQdiscBacklog, NicTelemetryError>
parse_qdisc_backlog(ExternalTelemetryText text) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(NicTelemetryError::EmptyInput);
    }
    QdiscBacklog backlog{};
    std::uint16_t admitted = 0;
    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        auto [key, value] = split_key_value(line);
        std::uint64_t parsed = 0;
        if (key == "backlog" && parse_leading_u64(value, parsed)) {
            backlog.backlog_bytes = parsed;
            backlog.backlog_packets = count_packets_suffix(value);
            ++admitted;
        } else if (contains_ci(key, "drops") && parse_leading_u64(value, parsed)) {
            backlog.drops = parsed;
            ++admitted;
        } else if (contains_ci(key, "overlimits") && parse_leading_u64(value, parsed)) {
            backlog.overlimits = parsed;
            ++admitted;
        }
    }
    if (admitted == 0) {
        return std::unexpected(NicTelemetryError::MissingRequiredField);
    }
    return declare_qdisc_backlog(backlog);
}

std::expected<DeclaredSysctlSnapshot, NicTelemetryError>
parse_sysctl_snapshot(ExternalTelemetryText text) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(NicTelemetryError::EmptyInput);
    }
    SysctlSnapshot snapshot{};
    std::uint16_t admitted = 0;
    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        auto [key, value] = split_key_value(line);
        std::uint64_t parsed64 = 0;
        std::uint32_t parsed32 = 0;
        if ((key == "net.core.rmem_max" || key == "rmem_max")
            && parse_u64(value, parsed64)) {
            snapshot.rmem_max_bytes = parsed64;
        } else if ((key == "net.core.wmem_max" || key == "wmem_max")
                   && parse_u64(value, parsed64)) {
            snapshot.wmem_max_bytes = parsed64;
        } else if ((key == "net.core.busy_poll" || key == "busy_poll")
                   && parse_u32(value, parsed32)) {
            snapshot.busy_poll_us = parsed32;
        } else if ((key == "net.ipv4.tcp_rmem_max" || key == "tcp_rmem_max")
                   && parse_u32(value, parsed32)) {
            snapshot.tcp_rmem_max_bytes = parsed32;
        } else if ((key == "net.ipv4.tcp_wmem_max" || key == "tcp_wmem_max")
                   && parse_u32(value, parsed32)) {
            snapshot.tcp_wmem_max_bytes = parsed32;
        } else {
            continue;
        }
        ++admitted;
    }
    if (admitted == 0) {
        return std::unexpected(NicTelemetryError::MissingRequiredField);
    }
    return declare_sysctl_snapshot(snapshot);
}

}  // namespace crucible::topology
