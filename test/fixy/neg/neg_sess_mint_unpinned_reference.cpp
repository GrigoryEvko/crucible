// mint_session_handle refuses an lvalue reference to a resource whose
// address is not stable.  The handle would store the reference, and a
// later move of the referent would leave every live handle dangling
// with no diagnostic at the point of the move.
//
// The same protocol and the same resource TYPE are accepted by value,
// so the refusal is the reference, not the protocol: this fixture fails
// only through the SessionResource clause.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
// Movable, so its address is not its identity.  A Pinned resource is
// what the concept admits by reference.
struct LooseWire {
    int n = 0;
};
}  // namespace

using Once = s::Send<Ping, s::End>;

int main() {
    LooseWire wire{};
    auto handle = s::mint_session_handle<Once, LooseWire&>(wire);
    (void)handle;
    return 0;
}
