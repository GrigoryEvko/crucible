// R2 receives a bool from R0 and then a number from R1.  On a mailbox the
// number from R1 can reach the head of the queue of R2 first.  R2 then
// waits for R0 behind a message that it cannot take yet, and nothing else
// takes that message.  The gate refuses the protocol on a mailbox.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_mailbox_head_blocks_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct Val {};
struct Nat {};
struct Mailbox {
    static constexpr s::Network session_network = s::Network::Mailbox;
};
}  // namespace neg_sess_network_mailbox_head_blocks_types

using namespace neg_sess_network_mailbox_head_blocks_types;

using HeadBlocks = g::Msg<R0, R2, Val, bool, g::Msg<R1, R2, Val, Nat, g::End>>;

int main() {
    s::ensure_carrier_implements<HeadBlocks, Mailbox>();
    return 0;
}
