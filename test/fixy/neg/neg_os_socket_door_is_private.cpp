// The one call to ::socket is a private member of SocketDoor, and its
// one friend is fixy::net::mint_socket.  A direct call to the door, which
// names no context, is refused.

#include <fixy/os/Socket.h>

int main() {
    [[maybe_unused]] auto opened = fixy::net::SocketDoor::open_<fixy::net::socket_kind::NetlinkRoute>();
    return 0;
}
