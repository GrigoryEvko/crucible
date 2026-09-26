// On a bag a stream of one label loses the order of its values.  R0
// sends X to R1 in each iteration of a loop, and R1 does not answer, so
// the message of the next iteration can be in the bag before R1 takes the
// message of this one.  R1 can then take the values in another order.
// The carrier gate refuses it.  Sprout(A) admits it on a bag, because its
// model has no values.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_bag_repeated_label_stream_types {
struct R0 {};
struct R1 {};
struct X {};
struct Bag {
    static constexpr s::Network session_network = s::Network::Bag;
};
}  // namespace neg_sess_network_bag_repeated_label_stream_types

using namespace neg_sess_network_bag_repeated_label_stream_types;

using Stream = g::Rec<g::Msg<R0, R1, X, int, g::Var>>;

int main() {
    s::ensure_carrier_implements<Stream, Bag>();
    return 0;
}
