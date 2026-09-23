// The mint refuses a Select whose label branches mix a keyed and a
// positional branch, because the protocol is not well-formed.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;

namespace {
struct Bob {};
struct Hello {};
struct Wire {};
}  // namespace

using Mixed = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<int, s::End>>;

int main() {
    auto handle = s::mint_session_handle<Mixed, Wire>(Wire{});
    (void)handle;
    return 0;
}
