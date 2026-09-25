// The socket mint, run against the kernel.
//
// The header pins the triple, the row and the gate at compile time.  This
// file checks what only a running kernel can answer: that the descriptor
// the mint returns is a socket of the kind the tag names, and that it
// does not survive execve.

#include <fixy/os/Socket.h>

#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <cstdio>
#include <utility>

namespace eff = foundation::effects;

namespace {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

[[nodiscard]] bool socket_option_is(int fd, int option, int expected) noexcept {
    int value = -1;
    ::socklen_t length = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, option, &value, &length) != 0) return false;
    return value == expected;
}

[[nodiscard]] int check_netlink_route_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    auto minted = fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx);
    if (!minted) {
        std::fprintf(stderr, "mint_socket<NetlinkRoute> failed: %s\n", minted.error().message().c_str());
        return 1;
    }
    fixy::fs::OwnedFd const& handle = minted->peek();
    if (!handle.is_open()) return 2;
    if (!socket_option_is(handle.get(), SO_DOMAIN, AF_NETLINK)) return 3;
    if (!socket_option_is(handle.get(), SO_TYPE, SOCK_RAW)) return 4;
    if (!socket_option_is(handle.get(), SO_PROTOCOL, NETLINK_ROUTE)) return 5;
    const int descriptor_flags = ::fcntl(handle.get(), F_GETFD);
    if (descriptor_flags < 0 || (descriptor_flags & FD_CLOEXEC) == 0) return 6;
    return 0;
}

}  // namespace

int main() {
    if (const int failed = check_netlink_route_socket(); failed != 0) {
        std::fprintf(stderr, "test_os_socket: check %d failed\n", failed);
        return failed;
    }
    std::puts("test_os_socket: passed");
    return 0;
}
