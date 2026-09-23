// Two branches of one Select name the same peer and label, with
// different payloads.  A label names one branch of a choice, as in a
// global type, so the protocol is not well-formed and no handle is
// minted for it.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;

namespace {
struct Bob {};
struct Hello {};
struct Wire {};
}  // namespace

using SameLabel = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<s::PeerMsg<Bob, Hello, bool>, s::End>>;

int main() {
    auto handle = s::mint_session_handle<SameLabel, Wire>(Wire{});
    (void)handle;
    return 0;
}
