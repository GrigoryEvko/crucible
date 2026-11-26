// mint_socket_pair opens only a kind of the local domain.  The kernel gives
// the two connected ends of socketpair(2) only for that domain, so the gate
// refuses a TCP stream socket.  The context here admits the full row, so the
// kind is the only reason for the refusal.

#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto pair = net::mint_socket_pair<net::socket_kind::TcpStream>(ctx);
    return 0;
}
