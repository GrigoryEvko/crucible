// On a bag, a later label of a choice can overtake an earlier one.  In
// this loop R0 chooses to go on or to stop.  R1 can take the stop label
// first, and the go-on label then stays in the bag with no receiver.
// Sprout(A) refuses the protocol on a bag, and the carrier gate refuses
// it too.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_bag_choice_types {
struct R0 {};
struct R1 {};
struct GoOn {};
struct Stop {};
struct Bag {
    static constexpr s::Network session_network = s::Network::Bag;
};
}  // namespace neg_sess_network_bag_choice_types

using namespace neg_sess_network_bag_choice_types;

using GoOnOrStop = g::Rec<g::Comm<R0, R1, g::Branch<GoOn, int, g::Var>, g::Branch<Stop, int, g::End>>>;

int main() {
    s::ensure_carrier_implements<GoOnOrStop, Bag>();
    return 0;
}
