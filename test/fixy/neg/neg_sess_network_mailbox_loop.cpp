// A loop in which R0 receives from R1 and then from R2, on each round.
// On a mailbox the message of R2 can reach the head of the queue of R0
// before the message of R1, so R0 waits for ever.  The gate refuses the
// protocol on a mailbox also when the two receives sit inside a loop.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_mailbox_loop_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct Val {};
struct Mailbox {
    static constexpr s::Network session_network = s::Network::Mailbox;
};
}  // namespace neg_sess_network_mailbox_loop_types

using namespace neg_sess_network_mailbox_loop_types;

using LoopTwoSenders = g::Rec<g::Msg<R1, R2, Val, int, g::Msg<R1, R0, Val, int, g::Msg<R2, R0, Val, int, g::Var>>>>;

int main() {
    s::ensure_carrier_implements<LoopTwoSenders, Mailbox>();
    return 0;
}
