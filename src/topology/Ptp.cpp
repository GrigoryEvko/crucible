#include <crucible/topology/Ptp.h>

namespace crucible::topology {

std::string_view ptp_error_name(PtpError error) noexcept {
    switch (error) {
        case PtpError::None:           return "None";
        case PtpError::ZeroNic:        return "ZeroNic";
        case PtpError::NonNicCog:      return "NonNicCog";
        case PtpError::InvalidClockFd: return "InvalidClockFd";
        case PtpError::NoTimestamp:    return "NoTimestamp";
        case PtpError::Degraded:       return "Degraded";
        default:                       return "<unknown PtpError>";
    }
}

std::string_view ptp_servo_state_name(PtpServoState state) noexcept {
    switch (state) {
        case PtpServoState::Unknown:      return "Unknown";
        case PtpServoState::Initializing: return "Initializing";
        case PtpServoState::Listening:    return "Listening";
        case PtpServoState::Slave:        return "Slave";
        case PtpServoState::Master:       return "Master";
        case PtpServoState::Faulty:       return "Faulty";
        case PtpServoState::Degraded:     return "Degraded";
        default:                          return "<unknown PtpServoState>";
    }
}

std::expected<TimestampedPacketView, PtpError>
timestamp_packet_view(std::span<const std::byte> payload,
                      PtpTimestampNs timestamp,
                      std::uint64_t sequence) noexcept {
    if (payload.empty()) {
        return std::unexpected(PtpError::Degraded);
    }
    return TimestampedPacketView{
        .payload = payload,
        .timestamp_ns = timestamp,
        .sequence = sequence,
    };
}

}  // namespace crucible::topology
