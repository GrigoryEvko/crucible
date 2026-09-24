// A read that returns the message itself waits inside its own call until
// the message is there, and the watch of fixy/session/Watch.h then does
// not see the wait.  The handle refuses such a read.  A read either polls
// and returns no value while nothing is there, or it takes the wait_scope
// that the handle opened for it.

#include <fixy/session/Handle.h>

#include <utility>

namespace s = ::fixy::session;

namespace neg_sess_recv_read_waits_in_secret_types {
struct Wire {
    int last_sent = 0;
};
}  // namespace neg_sess_recv_read_waits_in_secret_types

using namespace neg_sess_recv_read_waits_in_secret_types;

int main() {
    auto head = s::mint_session_handle<s::Recv<int, s::End>>(Wire{});
    auto [value, at_end] = std::move(head).recv([](Wire& wire) noexcept { return wire.last_sent; });
    static_cast<void>(value);
    static_cast<void>(std::move(at_end).close());
    return 0;
}
