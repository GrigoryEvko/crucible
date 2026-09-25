// mint_socket opens only a kind that has a specialization of
// socket_triple.  A kind tag with no triple has no domain, type or
// protocol, so the mint refuses it and ::socket never sees a guessed
// triple.  The context here admits the full row, so the kind is the only
// reason for the refusal.

#include <fixy/os/Socket.h>

namespace eff = foundation::effects;
namespace net = fixy::net;

namespace {
using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

struct NotASocketKind final {};
}  // namespace

int main() {
    IoBlockCtx ctx{eff::testing::test()};
    [[maybe_unused]] auto socket = net::mint_socket<NotASocketKind>(ctx);
    return 0;
}
