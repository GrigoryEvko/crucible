// On a bag the wire word of a message is its label alone.  R0 sends X
// with an int and then X with a double to R1, and nothing orders the two
// sends after the first receive.  Both can be in the bag, and the first
// receive of R1 can take the double.  The carrier gate refuses it.
// Sprout(A) admits it on a bag, because its message carries the payload
// type, and our wire word does not.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_bag_repeated_label_two_payloads_types {
struct R0 {};
struct R1 {};
struct X {};
struct Bag {
    static constexpr s::Network session_network = s::Network::Bag;
};
}  // namespace neg_sess_network_bag_repeated_label_two_payloads_types

using namespace neg_sess_network_bag_repeated_label_two_payloads_types;

using TwoPayloads = g::Msg<R0, R1, X, int, g::Msg<R0, R1, X, double, g::End>>;

int main() {
    s::ensure_carrier_implements<TwoPayloads, Bag>();
    return 0;
}
