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
    return ::fixy::time::nanos_from_timespec(stamp)
        .map([](std::uint64_t nanos) noexcept { return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(nanos); })
        .map_err(timestamp_error);
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
    return clock.caps().map_err([](::foundation::os::Errno) noexcept { return PtpError::ClockCapsUnavailable; });
}

// The reading is stamped as a PHC reading by the reader, which read it from
// the clock of its own descriptor.  The value keeps that provenance under
// the Ptp source tag.
::fixy::Result<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock,
                                                 ::foundation::effects::cap::Nondet nondet) noexcept {
    return clock.read(nondet)
        .map([](::fixy::PtpHwClockBytes<std::uint64_t> reading) noexcept {
            return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::move(reading).consume());
        })
        .map_err(timestamp_error);
}

// The door gives the part of the buffer that the kernel wrote, so the
// payload of the packet is that part.
::fixy::Result<TimestampedPacket, PtpError>
detail::packet_of_receive(::fixy::net::TimestampedReceive<::fixy::View<std::byte>> const& received) noexcept {
    using Packet = ::fixy::Result<TimestampedPacket, PtpError>;
    if (received.payload.size() == 0) {
        return ::fixy::err(PtpError::RecvFailed);
    }
    return received.control.match(
        [](::fixy::net::NoTimestampMessage) -> Packet { return ::fixy::err(PtpError::NoTimestamp); },
        [](::fixy::net::ShortTimestampMessage) -> Packet { return ::fixy::err(PtpError::MalformedTimestampControl); },
        [&received](::fixy::net::ReceiveStamps const& stamps) -> Packet {
            bool const has_hardware = is_stamped(stamps.raw_hardware);
            bool const has_software = is_stamped(stamps.software);
            if (!has_hardware && !has_software) {
                return ::fixy::err(PtpError::NoTimestamp);
            }
            CRUCIBLE_TRY(timestamp, timestamp_from_stamp(has_hardware ? stamps.raw_hardware : stamps.software));
            return TimestampedPacket{
                .payload = received.payload,
                .size = received.payload.size(),
                .timestamp_ns = timestamp,
                .hardware = has_hardware,
            };
        });
}

}  // namespace crucible::topology
