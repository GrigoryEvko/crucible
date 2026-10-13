// The session over a lazily established channel holds its Resource by
// lvalue reference, so the Resource must have an address that cannot
// change.  A plain struct can move, and the channel refuses it.  A
// handle over a raw pointer to such a struct would dangle after the
// move.

#include <fixy/channels/LazyEstablishedChannel.h>

namespace channels = ::fixy::channels;
namespace s = ::fixy::session;

namespace {
struct MovableWire {
    int sentinel = 0;
};
}  // namespace

int main() {
    channels::LazyEstablishedChannel<s::Send<int, s::End>, MovableWire> channel;
    return channel.is_established() ? 1 : 0;
}
