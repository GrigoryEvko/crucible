// On a mailbox, the senders of one receiver share one FIFO queue.  In
// this protocol R0 receives from R1 and then from R2.  R2 can send first,
// so its message reaches the head of the queue while R0 waits for R1,
// and R0 waits for ever.  Sprout(A) refuses the protocol on a mailbox,
// and the carrier gate refuses it too.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_mailbox_two_senders_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct Val {};
struct Mailbox {
    static constexpr s::Network session_network = s::Network::Mailbox;
};
}  // namespace neg_sess_network_mailbox_two_senders_types

using namespace neg_sess_network_mailbox_two_senders_types;

using TwoSenders = g::Msg<R1, R0, Val, bool, g::Msg<R2, R0, Val, bool, g::End>>;

int main() {
    s::ensure_carrier_implements<TwoSenders, Mailbox>();
    return 0;
}
