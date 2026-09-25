// The session over a lazily established channel holds its Resource by
// lvalue reference, so the Resource must have an address that cannot
// change.  A plain struct can move, and the channel refuses it.  The
// old channel held a raw pointer to such a struct and minted a handle
// over the pointer.

#include <fixy/handle/LazyEstablishedChannel.h>

namespace h = ::fixy::handle;
namespace s = ::fixy::session;

namespace {
struct MovableWire {
    int sentinel = 0;
};
}  // namespace

int main() {
    h::LazyEstablishedChannel<s::Send<int, s::End>, MovableWire> channel;
    return channel.is_established() ? 1 : 0;
}
