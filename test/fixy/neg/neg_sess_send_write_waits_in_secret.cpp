// A write that returns void can wait inside its own call, and the watch
// of fixy/session/Watch.h then does not see the wait.  The handle refuses
// such a write.  A write either tries and returns false while it has no
// room, or it takes the wait_scope that the handle opened for it.

#include <fixy/session/Handle.h>

#include <utility>

namespace s = ::fixy::session;

namespace neg_sess_send_write_waits_in_secret_types {
struct Wire {
    int last_sent = 0;
};
}  // namespace neg_sess_send_write_waits_in_secret_types

using namespace neg_sess_send_write_waits_in_secret_types;

int main() {
    auto head = s::mint_session_handle<s::Send<int, s::End>>(Wire{});
    auto at_end = std::move(head).send(1, [](Wire& wire, int&& value) noexcept { wire.last_sent = value; });
    static_cast<void>(std::move(at_end).close());
    return 0;
}
