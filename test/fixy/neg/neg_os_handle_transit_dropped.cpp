// A transit of a Handle ends in its arrive on the other thread
// (fixy/os/Handle.h).  It owes a consume at its end, as a live fixy::Linear
// does, so the generic drop door of foundation/Linear.h refuses it: a drop
// would close the descriptor in silence, and no thread would get the Handle.

#include <fixy/os/Socket.h>
#include <foundation/Linear.h>

#include <utility>

int main() {
    fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream> socket{};
    auto transit = std::move(socket).depart();
    ::foundation::linear::drop(std::move(transit));
    return 0;
}
