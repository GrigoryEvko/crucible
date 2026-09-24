// A crash-stop protocol, in which R1 detects a crash of R0, binds to a
// mailbox.  The theorem of the crash-stop paper holds on per-pair FIFO,
// and Li and Wies model no crash on a mailbox, so the gate refuses it.
// The same protocol with the same reliable set binds to per-pair FIFO.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_crash_stop_on_mailbox_types {
struct R0 {};
struct R1 {};
struct Val {};
struct Mailbox {
    static constexpr s::Network session_network = s::Network::Mailbox;
};
}  // namespace neg_sess_network_crash_stop_on_mailbox_types

using namespace neg_sess_network_crash_stop_on_mailbox_types;

using Detects = g::Comm<R0, R1, g::Branch<Val, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;

int main() {
    s::ensure_carrier_implements<Detects, Mailbox, s::NoReliableRoles>();
    return 0;
}
