#include <crucible/topology/Telemetry.h>

#include <crucible/topology/TextFields.h>

namespace crucible::topology {

namespace {

using ::fixy::TextView;

// A telemetry line is a key and a value at the two sides of a colon, an
// equals sign or a space, in that order of choice.
[[nodiscard]] constexpr text::Field split_key_value(TextView line) noexcept { return text::split_field(line, ":= "); }

// The value of the first word of text, when each character of that word is
// a decimal digit and the value fits T.
template <typename T>
[[nodiscard]] constexpr ::fixy::Option<T> parse_word(TextView value) noexcept {
    TextView const word = text::first_word(value);
    if (word.size() == 0 || text::leading_digits(word) != word.size()) {
        return ::fixy::none;
    }
    return ::fixy::parse<T>(word).ok();
}

// The value of the decimal digits at the start of the first word of text.
[[nodiscard]] constexpr ::fixy::Option<std::uint64_t> parse_leading_u64(TextView value) noexcept {
    return text::leading_u64(text::first_word(value));
}

// The packet count of a qdisc backlog such as "1024b 3p requeues 0": the
// number of the first word that ends in "p" and starts with a number that
// fits 32 bits.  Zero when no word is such a count.
[[nodiscard]] constexpr std::uint32_t count_packets_suffix(TextView value) noexcept {
    TextView rest = text::trim(value);
    while (rest.size() != 0) {
        TextView const word = text::first_word(rest);
        rest = text::trim(text::tail(rest, word.size()));
        if (word.size() < 2 || !::fixy::ends_with(word, "p")) {
            continue;
        }
        for (std::uint32_t const packets :
             ::fixy::parse<std::uint32_t>(text::head(word, text::leading_digits(word))).ok()) {
            return packets;
        }
    }
    return 0;
}

}  // namespace

::fixy::Result<DeclaredNetdevCounters, NicTelemetryError> parse_netdev_counters(ExternalTelemetryText text) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(NicTelemetryError::EmptyInput);
    }
    NetdevCounters counters{};
    std::uint16_t admitted = 0;
    while (input.size() != 0) {
        text::Field const field = split_key_value(text::take_line(input));
        for (std::uint64_t const parsed : parse_word<std::uint64_t>(field.value)) {
            if (field.key == "rx_bytes") {
                counters.rx_bytes = parsed;
            } else if (field.key == "tx_bytes") {
                counters.tx_bytes = parsed;
            } else if (field.key == "rx_packets") {
                counters.rx_packets = parsed;
            } else if (field.key == "tx_packets") {
                counters.tx_packets = parsed;
            } else if (field.key == "rx_dropped") {
                counters.rx_dropped = parsed;
            } else if (field.key == "tx_dropped") {
                counters.tx_dropped = parsed;
            } else if (field.key == "rx_errors") {
                counters.rx_errors = parsed;
            } else if (field.key == "tx_errors") {
                counters.tx_errors = parsed;
            } else if (field.key == "rx_fifo_errors") {
                counters.rx_fifo_errors = parsed;
            } else if (field.key == "tx_fifo_errors") {
                counters.tx_fifo_errors = parsed;
            } else {
                continue;
            }
            ++admitted;
        }
    }
    if (admitted == 0) {
        return ::fixy::err(NicTelemetryError::MissingRequiredField);
    }
    return declare_netdev_counters(counters);
}

::fixy::Result<DeclaredQdiscBacklog, NicTelemetryError> parse_qdisc_backlog(ExternalTelemetryText text) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(NicTelemetryError::EmptyInput);
    }
    QdiscBacklog backlog{};
    std::uint16_t admitted = 0;
    while (input.size() != 0) {
        text::Field const field = split_key_value(text::take_line(input));
        for (std::uint64_t const parsed : parse_leading_u64(field.value)) {
            if (field.key == "backlog") {
                backlog.backlog_bytes = parsed;
                backlog.backlog_packets = count_packets_suffix(field.value);
            } else if (text::contains_ignoring_case(field.key, "drops")) {
                backlog.drops = parsed;
            } else if (text::contains_ignoring_case(field.key, "overlimits")) {
                backlog.overlimits = parsed;
            } else {
                continue;
            }
            ++admitted;
        }
    }
    if (admitted == 0) {
        return ::fixy::err(NicTelemetryError::MissingRequiredField);
    }
    return declare_qdisc_backlog(backlog);
}

::fixy::Result<DeclaredSysctlSnapshot, NicTelemetryError> parse_sysctl_snapshot(ExternalTelemetryText text) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(NicTelemetryError::EmptyInput);
    }
    SysctlSnapshot snapshot{};
    std::uint16_t admitted = 0;
    while (input.size() != 0) {
        text::Field const field = split_key_value(text::take_line(input));
        TextView const key = field.key;
        bool is_admitted = false;
        if (key == "net.core.rmem_max" || key == "rmem_max") {
            for (std::uint64_t const parsed : parse_word<std::uint64_t>(field.value)) {
                snapshot.rmem_max_bytes = parsed;
                is_admitted = true;
            }
        } else if (key == "net.core.wmem_max" || key == "wmem_max") {
            for (std::uint64_t const parsed : parse_word<std::uint64_t>(field.value)) {
                snapshot.wmem_max_bytes = parsed;
                is_admitted = true;
            }
        } else if (key == "net.core.busy_poll" || key == "busy_poll") {
            for (std::uint32_t const parsed : parse_word<std::uint32_t>(field.value)) {
                snapshot.busy_poll_us = parsed;
                is_admitted = true;
            }
        } else if (key == "net.ipv4.tcp_rmem_max" || key == "tcp_rmem_max") {
            for (std::uint32_t const parsed : parse_word<std::uint32_t>(field.value)) {
                snapshot.tcp_rmem_max_bytes = parsed;
                is_admitted = true;
            }
        } else if (key == "net.ipv4.tcp_wmem_max" || key == "tcp_wmem_max") {
            for (std::uint32_t const parsed : parse_word<std::uint32_t>(field.value)) {
                snapshot.tcp_wmem_max_bytes = parsed;
                is_admitted = true;
            }
        }
        if (is_admitted) {
            ++admitted;
        }
    }
    if (admitted == 0) {
        return ::fixy::err(NicTelemetryError::MissingRequiredField);
    }
    return declare_sysctl_snapshot(snapshot);
}

}  // namespace crucible::topology
