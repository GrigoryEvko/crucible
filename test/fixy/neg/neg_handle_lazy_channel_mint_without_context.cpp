// The session mint of a lazily established channel takes an execution
// context.  A value that is not a context cannot stand in for one, so
// the mint is refused before the gate reads the protocol.
//
// The channel is established correctly first, so nothing about the
// publication is at fault here — only the argument.

#include <fixy/handle/LazyEstablishedChannel.h>

namespace h = ::fixy::handle;
namespace s = ::fixy::session;

namespace {
struct Wire : ::foundation::Pinned<Wire> {
    int sentinel = 0;
};
}  // namespace

int main() {
    h::LazyEstablishedChannel<s::Send<int, s::End>, Wire> channel;
    Wire wire{};
    channel.establish(wire);

    auto head = channel.mint_established_session(42);
    return head.has_value() ? 0 : 1;
}
