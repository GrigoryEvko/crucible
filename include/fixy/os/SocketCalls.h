#pragma once

// The calls of the socket door src/fixy/os/Socket.cpp.  The door issues
// them with the raw primitive of foundation/os/Syscall.h, and this header
// includes no system header, so the door file holds only the Linux UAPI
// header <linux/tcp.h>.  fixy/os/Socket.h gives the gated calls over them.
//
// Each call takes the descriptor of a socket and gives the error of the
// kernel in its Result.  A call that a signal interrupts runs again.  A
// descriptor below zero gives EBADF and makes no system call.
//
// THE KERNEL WORDS
//   The timestamp calls need words and structs of <sys/socket.h>,
//   <linux/net_tstamp.h>, <linux/sockios.h> and <net/if.h>.  The door file
//   includes none of them, so this header gives each one as a word or as a
//   plain record of a 64-bit build.  An address in a record is a word, as
//   the primitive takes it.  The check file of fixy/os/Socket.h compares
//   each word, each size and each offset with those headers.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>
#include <foundation/os/FutexWords.h>

#include <cstddef>
#include <cstdint>

namespace fixy::net {

// The TCP options of the socket door.  The door file maps each one to its
// name of <linux/tcp.h>: TCP_CONGESTION, TCP_RTO_MIN_US, TCP_INFO (the
// kernel struct tcp_info) and TCP_CC_INFO (the kernel union tcp_cc_info).
enum class TcpOption : std::uint8_t {
    congestion,
    rto_min_us,
    info,
    congestion_info,
};

// The control data of a receive holds no SCM_TIMESTAMPING message.
struct NoTimestampMessage final {};

// The control data holds an SCM_TIMESTAMPING message that is too short for
// its three stamps.  The kernel cuts a message short when the control buffer
// has no room for all of it.
struct ShortTimestampMessage final {};

// The stamps of an SCM_TIMESTAMPING message (struct scm_timestamping).  The
// kernel writes the software stamp at index 0 and the raw stamp of the
// hardware clock of the NIC at index 2.  Index 1 is zero since Linux 3.17,
// so it has no field.  A stamp that the kernel did not take is zero.
struct ReceiveStamps final {
    ::foundation::os::KernelTimespec software{};
    ::foundation::os::KernelTimespec raw_hardware{};
};

// What the control data of a receive tells about the time of the datagram.
using TimestampControl = ::foundation::core::OneOf<NoTimestampMessage, ShortTimestampMessage, ReceiveStamps>;

// One receive of receive_with_timestamps: the count of the bytes that the
// kernel wrote into the buffer, and the stamps of the control data.
struct TimestampedReceive final {
    std::size_t count = 0;
    TimestampControl control{NoTimestampMessage{}};
};

}  // namespace fixy::net

namespace fixy::net::detail {

// SOL_SOCKET: the level of the options of each socket.
inline constexpr int socket_level = 1;

// SO_TIMESTAMPING of a 64-bit build.  The kernel gives the stamps in a
// control message of the same type (SCM_TIMESTAMPING).
inline constexpr int timestamping_option = 37;

// The SO_TIMESTAMPING flags of set_socket_timestamping:
// SOF_TIMESTAMPING_RX_HARDWARE, RX_SOFTWARE, SOFTWARE, RAW_HARDWARE,
// OPT_CMSG and OPT_TSONLY.
inline constexpr std::uint32_t receive_timestamping_flags =
    (1U << 2U) | (1U << 3U) | (1U << 4U) | (1U << 6U) | (1U << 10U) | (1U << 11U);

// SIOCSHWTSTAMP: the request that sets the hardware stamps of a NIC.
inline constexpr std::uint64_t set_hardware_timestamping_request = 0x89B0U;

// HWTSTAMP_TX_ON and HWTSTAMP_FILTER_ALL: a stamp of each sent packet and of
// each received packet.
inline constexpr std::int32_t hardware_transmit_on = 1;
inline constexpr std::int32_t hardware_filter_all = 1;

// IFNAMSIZ: the bytes of the name of a network interface, with its zero.
inline constexpr std::size_t interface_name_capacity = 16;

// The layout of struct hwtstamp_config.
struct HardwareTimestampConfig final {
    std::int32_t flags = 0;
    std::int32_t transmit = 0;
    std::int32_t receive_filter = 0;
};

// The layout of struct ifreq.  The union after the name is 24 bytes, the
// size of its largest member struct ifmap.  SIOCSHWTSTAMP reads the address
// of the config from the first word of the union (ifr_data).
struct InterfaceRequest final {
    ::foundation::core::FixedArray<char, interface_name_capacity> name{};
    std::uint64_t data = 0;
    std::uint64_t union_second_word = 0;
    std::uint64_t union_third_word = 0;
};

// The layout of struct iovec: the address and the count of one buffer.
struct IoVector final {
    std::uint64_t base = 0;
    std::uint64_t length = 0;
};

// The layout of struct msghdr.  The kernel writes the count of the control
// bytes into control_length and the flags of the receive into flags.
struct MessageHeader final {
    std::uint64_t name = 0;
    std::uint32_t name_length = 0;
    std::uint32_t pad_after_name_length = 0;
    std::uint64_t vectors = 0;
    std::uint64_t vector_count = 0;
    std::uint64_t control = 0;
    std::uint64_t control_length = 0;
    std::int32_t flags = 0;
    std::uint32_t pad_after_flags = 0;
};

// The layout of struct cmsghdr, the header of one control message.  The
// length counts the header and the data, and the next message starts at the
// length rounded up to control_alignment.
struct ControlHeader final {
    std::uint64_t length = 0;
    std::int32_t level = 0;
    std::int32_t type = 0;
};

inline constexpr std::size_t control_alignment = 8;

// The bytes of an SCM_TIMESTAMPING message with its three stamps
// (CMSG_LEN of struct scm_timestamping).
inline constexpr std::size_t timestamping_message_bytes =
    sizeof(ControlHeader) + 3 * sizeof(::foundation::os::KernelTimespec);

// The value of the bytes of bytes at offset, or no value when they do not
// fit.  decode refuses a Value that some bytes do not make.
template <class Value>
[[nodiscard]] constexpr ::foundation::core::Option<Value> decode_at_(::foundation::core::View<std::byte const> bytes,
                                                                     std::size_t offset) noexcept {
    using Decoded = ::foundation::core::Option<Value>;
    return bytes.template window<sizeof(Value)>(offset).match(
        [](::foundation::core::View<std::byte const, sizeof(Value)> window) -> Decoded {
            ::foundation::core::FixedArray<std::byte, sizeof(Value)> word{};
            for (::foundation::core::Index<sizeof(Value)> const index : word.indices()) {
                word.at(index) = window.at(index);
            }
            return ::foundation::core::decode<Value>(word);
        },
        []() -> Decoded { return ::foundation::core::none; });
}

// The stamps of the control data of one receive, the bytes that the kernel
// wrote.  The walk reads the messages in order, as CMSG_FIRSTHDR and
// CMSG_NXTHDR do, and the first SCM_TIMESTAMPING message gives the result.
// A header that gives less than its own size, or more than the bytes after
// it, ends the walk, so each step moves forward and stays in the bytes.
// Complexity: linear in the count of the messages.
[[nodiscard]] constexpr TimestampControl
read_timestamp_control_(::foundation::core::View<std::byte const> control) noexcept {
    std::size_t offset = 0;
    while (control.size() - offset >= sizeof(ControlHeader)) {
        ControlHeader const header =
            decode_at_<ControlHeader>(control, offset).expect("the loop leaves room for the header");
        bool const is_inside = header.length >= sizeof(ControlHeader) && header.length <= control.size() - offset;
        if (!is_inside) {
            return NoTimestampMessage{};
        }
        if (header.level == socket_level && header.type == timestamping_option) {
            if (header.length < timestamping_message_bytes) {
                return ShortTimestampMessage{};
            }
            std::size_t const stamps = offset + sizeof(ControlHeader);
            return ReceiveStamps{
                .software = decode_at_<::foundation::os::KernelTimespec>(control, stamps)
                                .expect("the message holds its three stamps"),
                .raw_hardware = decode_at_<::foundation::os::KernelTimespec>(
                                    control, stamps + 2 * sizeof(::foundation::os::KernelTimespec))
                                    .expect("the message holds its three stamps"),
            };
        }
        std::size_t const step = (header.length + control_alignment - 1) / control_alignment * control_alignment;
        if (step > control.size() - offset) {
            return NoTimestampMessage{};
        }
        offset += step;
    }
    return NoTimestampMessage{};
}

// Sets the TCP option option of SOCKET to the SIZE bytes at VALUE.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_tcp_option_(int socket, TcpOption option, void const* value, std::uint32_t size) noexcept;

// Reads the TCP option option of SOCKET into the CAPACITY bytes at VALUE,
// and gives the count of the bytes that the kernel wrote.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_tcp_option_(int socket, TcpOption option, void* value, std::uint32_t capacity) noexcept;

// Sets the option NAME of LEVEL of SOCKET to the SIZE bytes at VALUE.
// fixy/os/Socket.h gives LEVEL and NAME from <sys/socket.h>.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_option_(int socket, int level, int name, void const* value, std::uint32_t size) noexcept;

// Binds SOCKET to the SIZE bytes of the address at ADDRESS.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
bind_address_(int socket, void const* address, std::uint32_t size) noexcept;

// Sends the COUNT bytes at FIRST with the flags FLAGS, and gives the count
// that the kernel took (sendto with no address).
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
send_with_flags_(int socket, void const* first, std::size_t count, int flags) noexcept;

// Receives at most COUNT bytes at FIRST with the flags FLAGS, and gives the
// count of the bytes (recvfrom with no address).
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
receive_with_flags_(int socket, void* first, std::size_t count, int flags) noexcept;

// Sets the hardware stamps of the NIC of the interface INTERFACE_NAME
// (SIOCSHWTSTAMP with HWTSTAMP_TX_ON and HWTSTAMP_FILTER_ALL).  A name of
// interface_name_capacity characters or more gives ENAMETOOLONG and makes no
// system call.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_hardware_timestamping_(int socket, ::foundation::core::TextView interface_name) noexcept;

// Receives at most COUNT bytes at FIRST with the flags FLAGS, and reads the
// stamps of the control data (recvmsg with one buffer and no address).
[[nodiscard]] ::foundation::core::Result<TimestampedReceive, ::foundation::os::Errno>
receive_with_timestamps_(int socket, void* first, std::size_t count, int flags) noexcept;

}  // namespace fixy::net::detail
