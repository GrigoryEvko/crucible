// The departure of a Handle gives a transit, which ends in its arrive on the
// other thread (fixy/os/Handle.h).  An expression that discards the transit
// would close the descriptor in silence, and no thread would get the Handle.
// The departure and the class are nodiscard, and the build makes each warning
// an error.

#include <fixy/os/Socket.h>

#include <utility>

int main() {
    fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream> socket{};
    std::move(socket).depart();
    return 0;
}
