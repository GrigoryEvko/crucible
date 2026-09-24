// A Send of a PeerMsg is keyed, as the projection writes a message: its
// label word is the whole message, so the step sends no value.  A call
// that passes the message as a value is refused.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

#include <cstddef>

namespace s = fixy::session;

namespace {
struct Bob {};
struct Ping {};
struct Wire {};
}  // namespace

using Ask = s::Send<s::PeerMsg<Bob, Ping, int>, s::Recv<int, s::End>>;

int main() {
    auto handle = s::mint_session_handle<Ask, Wire>(Wire{});
    auto waiting = std::move(handle).send(s::PeerMsg<Bob, Ping, int>{}, [](Wire&, std::size_t) noexcept {});
    (void)waiting;
    return 0;
}
