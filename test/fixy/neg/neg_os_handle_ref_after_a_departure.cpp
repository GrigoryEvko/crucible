// A borrow of a Handle is read after depart(), which gives the descriptor to a
// transit for another thread and advances the generation of its number.  The
// door takes the Handle by an rvalue reference, so it ends each borrow of the
// number, and the rule view-escape refuses the read.

#include <fixy/os/Socket.h>

#include <utility>

namespace {
using TcpSocket = ::fixy::net::SocketHandle<::fixy::net::socket_kind::TcpStream>;
}  // namespace

int main() {
    TcpSocket socket{};
    auto const borrow = socket.ref();
    auto transit = std::move(socket).depart();
    TcpSocket const arrived = std::move(transit).arrive();
    return borrow.is_open() && arrived.is_open() ? 1 : 0;
}
