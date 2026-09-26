// On a bag the wire word of a message is its label alone, so the sender
// is not on the wire either.  R0 and R1 each send X to R2, and nothing
// orders the two sends.  The first receive of R2 can take the message of
// R1 as the message of R0.  The carrier gate refuses it.  Sprout(A)
// admits it on a bag, because its message carries the sender, and our
// wire word does not.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_bag_repeated_label_two_senders_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct X {};
struct Bag {
    static constexpr s::Network session_network = s::Network::Bag;
};
}  // namespace neg_sess_network_bag_repeated_label_two_senders_types

using namespace neg_sess_network_bag_repeated_label_two_senders_types;

using TwoSenders = g::Msg<R0, R2, X, int, g::Msg<R1, R2, X, int, g::End>>;

int main() {
    s::ensure_carrier_implements<TwoSenders, Bag>();
    return 0;
}
