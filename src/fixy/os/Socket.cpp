// The socket door of fixy (fixy/os/SocketCalls.h): the options, the bind,
// the send and the receive of a socket, and the stamps of a receive.  It
// issues the raw primitive of foundation/os/Syscall.h.  It is the one
// includer of <linux/tcp.h>, for the names of the TCP options.  It includes
// no header of the C library, because some declarations of the two kinds of
// header conflict.  The words and the records of the timestamp calls come
// from fixy/os/SocketCalls.h (THE KERNEL WORDS).  A call that a signal
// interrupts runs again.

#include <fixy/os/SocketCalls.h>

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <linux/tcp.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::net::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

using UnitResult = core::Result<core::Unit, os::Errno>;
using CountResult = core::Result<std::size_t, os::Errno>;

// The level of the TCP options: the IANA protocol number of TCP, which
// IPPROTO_TCP and SOL_TCP name.  <linux/tcp.h> gives the names of the
// options and no level.  The test of the socket door sets and reads each
// option on a TCP socket of the kernel, so a wrong level fails it.
constexpr int kTcpLevel = 6;

// The word of a call that a signal interrupted.
constexpr long kInterrupted = -static_cast<long>(std::to_underlying(os::Errno::eintr));

// The descriptor of a closed handle, which gets EBADF with no system call.
[[nodiscard]] constexpr bool is_closed(int socket) noexcept { return socket < 0; }

[[nodiscard]] UnitResult unit_of(long word) noexcept {
    return os::result_of(word).match([](std::uint64_t) -> UnitResult { return core::Unit{}; },
                                     [](os::Errno error) -> UnitResult { return core::err(error); });
}

[[nodiscard]] CountResult count_of(long word) noexcept {
    return os::result_of(word).match([](std::uint64_t count) -> CountResult { return static_cast<std::size_t>(count); },
                                     [](os::Errno error) -> CountResult { return core::err(error); });
}

// The name of a TCP option in <linux/tcp.h>.
[[nodiscard]] constexpr int name_of(TcpOption option) noexcept {
    switch (option) {
        case TcpOption::congestion:
            return TCP_CONGESTION;
        case TcpOption::rto_min_us:
            return TCP_RTO_MIN_US;
        case TcpOption::info:
            return TCP_INFO;
        case TcpOption::congestion_info:
            return TCP_CC_INFO;
        default:
            return -1;
    }
}

}  // namespace

UnitResult set_tcp_option_(int socket, TcpOption option, void const* value, std::uint32_t size) noexcept {
    return set_option_(socket, kTcpLevel, name_of(option), value, size);
}

CountResult read_tcp_option_(int socket, TcpOption option, void* value, std::uint32_t capacity) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    std::uint32_t length = capacity;
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::getsockopt, socket, kTcpLevel, name_of(option), value,
                               static_cast<void const*>(&length));
    } while (word == kInterrupted);
    return unit_of(word).match([length](core::Unit) -> CountResult { return std::size_t{length}; },
                               [](os::Errno error) -> CountResult { return core::err(error); });
}

UnitResult set_option_(int socket, int level, int name, void const* value, std::uint32_t size) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::setsockopt, socket, level, name, value, size);
    } while (word == kInterrupted);
    return unit_of(word);
}

UnitResult bind_address_(int socket, void const* address, std::uint32_t size) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::bind, socket, address, size);
    } while (word == kInterrupted);
    return unit_of(word);
}

CountResult send_with_flags_(int socket, void const* first, std::size_t count, int flags) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::sendto, socket, first, count, flags, 0, 0);
    } while (word == kInterrupted);
    return count_of(word);
}

CountResult receive_with_flags_(int socket, void* first, std::size_t count, int flags) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::recvfrom, socket, first, count, flags, 0, 0);
    } while (word == kInterrupted);
    return count_of(word);
}

UnitResult set_hardware_timestamping_(int socket, core::TextView interface_name) noexcept {
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    if (interface_name.size() >= interface_name_capacity) {
        return core::err(os::Errno::enametoolong);
    }
    HardwareTimestampConfig const config{
        .flags = 0,
        .transmit = hardware_transmit_on,
        .receive_filter = hardware_filter_all,
    };
    // The name fills the front of the name field, and the zero of the empty
    // field ends it.
    InterfaceRequest request{};
    core::copy(request.name.view().window(0, interface_name.size()).expect("the name is below the capacity"),
               core::detail::view_over_(core::detail::text_first_(interface_name), interface_name.size()))
        .expect("the window has the count of the name");
    request.data = os::Word{static_cast<void const*>(&config)}.bits();
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::ioctl, socket, set_hardware_timestamping_request,
                               static_cast<void const*>(&request));
    } while (word == kInterrupted);
    return unit_of(word);
}

core::Result<TimestampedReceive, os::Errno> receive_with_timestamps_(int socket, void* first, std::size_t count,
                                                                     int flags) noexcept {
    using Received = core::Result<TimestampedReceive, os::Errno>;
    if (is_closed(socket)) {
        return core::err(os::Errno::ebadf);
    }
    // 256 bytes hold the SCM_TIMESTAMPING message and the other messages
    // that a receive can carry.  A cut message gives ShortTimestampMessage.
    core::FixedArray<std::byte, 256> control{};
    IoVector const vector{.base = os::Word{static_cast<void const*>(first)}.bits(), .length = count};
    MessageHeader message{
        .vectors = os::Word{static_cast<void const*>(&vector)}.bits(),
        .vector_count = 1,
        .control = os::Word{static_cast<void const*>(first_of_(core::detail::ViewDoor::key(), control.view()))}.bits(),
        .control_length = control.size(),
    };
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::recvmsg, socket, static_cast<void const*>(&message), flags);
    } while (word == kInterrupted);
    return count_of(word).match(
        [&message, &control, count](std::size_t received) -> Received {
            std::size_t const written = core::min(static_cast<std::size_t>(message.control_length), control.size());
            return TimestampedReceive{
                .count = core::min(received, count),
                .control = read_timestamp_control_(
                    control.view().window(0, written).expect("the kernel writes at most the control capacity")),
            };
        },
        [](os::Errno error) -> Received { return core::err(error); });
}

}  // namespace fixy::net::detail
