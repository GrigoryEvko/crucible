#pragma once

// The calls of the socket door src/fixy/os/Socket.cpp.  The door issues
// them with the raw primitive of foundation/os/Syscall.h, and this header
// includes no system header, so the door file holds only the Linux UAPI
// header <linux/tcp.h>.  fixy/os/Socket.h gives the gated calls over them.
//
// Each call takes the descriptor of a socket and gives the error of the
// kernel in its Result.  A call that a signal interrupts runs again.  A
// descriptor below zero gives EBADF and makes no system call.

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>

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

}  // namespace fixy::net

namespace fixy::net::detail {

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

}  // namespace fixy::net::detail
