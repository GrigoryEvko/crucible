#include <crucible/topology/Ptp.h>

#include <cstddef>
#include <cstdint>

namespace crucible::topology {

namespace {

// A stamp that the kernel did not take is zero.
[[nodiscard]] constexpr bool is_stamped(::foundation::os::KernelTimespec const& stamp) noexcept {
    return stamp.seconds != 0 || stamp.nanoseconds != 0;
}

// The conversion refuses a count past 64 bits with eoverflow and each other
// malformed stamp with erange.  A failed read gives the error number of the
// clock call.
[[nodiscard]] PtpError timestamp_error(::foundation::os::Errno error) noexcept {
    return error == ::foundation::os::Errno::eoverflow ? PtpError::TimestampOverflow : PtpError::ClockReadFailed;
}

[[nodiscard]] ::fixy::Result<PtpTimestampNs, PtpError>
timestamp_from_stamp(::foundation::os::KernelTimespec const& stamp) noexcept {
    using Stamped = ::fixy::Result<PtpTimestampNs, PtpError>;
    return ::fixy::time::nanos_from_timespec(stamp).match(
        [](std::uint64_t nanos) noexcept -> Stamped { return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(nanos); },
        [](::foundation::os::Errno error) noexcept -> Stamped { return ::fixy::err(timestamp_error(error)); });
}

}  // namespace

::fixy::Result<TimestampedPacketView, PtpError> timestamp_packet_view(::fixy::View<std::byte const> payload,
                                                                      PtpTimestampNs timestamp,
                                                                      std::uint64_t sequence) noexcept {
    if (payload.size() == 0) {
        return ::fixy::err(PtpError::Degraded);
    }
    return TimestampedPacketView{
        .payload = payload,
        .timestamp_ns = timestamp,
        .sequence = sequence,
    };
}

::fixy::Result<PtpClockCaps, PtpError> query_ptp_clock_caps(PtpClockReader const& clock) noexcept {
    using Queried = ::fixy::Result<PtpClockCaps, PtpError>;
    return clock.caps().match(
        [](PtpClockCaps caps) noexcept -> Queried { return caps; },
        [](::foundation::os::Errno) noexcept -> Queried { return ::fixy::err(PtpError::ClockCapsUnavailable); });
}

// The reading is stamped as a PHC reading by the reader, which read it from
// the clock of its own descriptor.  The value keeps that provenance under
// the Ptp source tag.
::fixy::Result<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock) noexcept {
    using Stamped = ::fixy::Result<PtpTimestampNs, PtpError>;
    return clock.read().match(
        [](::fixy::PtpHwClockBytes<std::uint64_t> reading) noexcept -> Stamped {
            return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::move(reading).consume());
        },
        [](::foundation::os::Errno error) noexcept -> Stamped { return ::fixy::err(timestamp_error(error)); });
}

// The door gives a count of at most the size of buffer, so the window of
// the payload fits.
::fixy::Result<TimestampedPacket, PtpError>
detail::packet_of_receive(::fixy::View<std::byte> buffer, ::fixy::net::TimestampedReceive const& received) noexcept {
    using Packet = ::fixy::Result<TimestampedPacket, PtpError>;
    if (received.count == 0) {
        return ::fixy::err(PtpError::RecvFailed);
    }
    return received.control.match(
        [](::fixy::net::NoTimestampMessage) -> Packet { return ::fixy::err(PtpError::NoTimestamp); },
        [](::fixy::net::ShortTimestampMessage) -> Packet { return ::fixy::err(PtpError::MalformedTimestampControl); },
        [buffer, &received](::fixy::net::ReceiveStamps const& stamps) -> Packet {
            bool const has_hardware = is_stamped(stamps.raw_hardware);
            bool const has_software = is_stamped(stamps.software);
            if (!has_hardware && !has_software) {
                return ::fixy::err(PtpError::NoTimestamp);
            }
            CRUCIBLE_TRY(timestamp, timestamp_from_stamp(has_hardware ? stamps.raw_hardware : stamps.software));
            return TimestampedPacket{
                .payload = buffer.window(0, received.count).expect("the door gives at most the size of the buffer"),
                .size = received.count,
                .timestamp_ns = timestamp,
                .hardware = has_hardware,
            };
        });
}

}  // namespace crucible::topology
