#pragma once

// A TCP socket of the socket door for a test.  A test that gives the socket
// to a table of cntp passes the shared ref of it (share_socket of
// fixy/os/Socket.h), and keeps the handle open while the table holds the ref.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>
#include <foundation/os/Errno.h>

#include <utility>

namespace crucible::test {

using TcpSocket = ::fixy::net::SocketHandle<::fixy::net::socket_kind::TcpStream>;

// A TCP socket of mint_socket, or an empty handle when the open failed.
template <class Ctx>
    requires ::fixy::net::CtxFitsSocketMint<Ctx, ::fixy::net::socket_kind::TcpStream>
[[nodiscard]] TcpSocket open_tcp_socket(Ctx const& ctx) noexcept {
    return ::fixy::net::mint_socket<::fixy::net::socket_kind::TcpStream>(ctx).match(
        [](::fixy::Linear<TcpSocket> opened) -> TcpSocket { return std::move(opened).consume(); },
        [](::foundation::os::Errno) -> TcpSocket { return TcpSocket{}; });
}

}  // namespace crucible::test
