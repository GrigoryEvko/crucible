// A carrier that states no network model gets no default.  The gate
// cannot say that any protocol is implementable on it, so it refuses the
// binding, even for a protocol with one message.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_carrier_undeclared_types {
struct R0 {};
struct R1 {};
struct Val {};
struct PlainQueue {
    int depth = 0;
};
}  // namespace neg_sess_network_carrier_undeclared_types

using namespace neg_sess_network_carrier_undeclared_types;

using OneMessage = g::Msg<R0, R1, Val, int, g::End>;

int main() {
    s::ensure_carrier_implements<OneMessage, PlainQueue>();
    return 0;
}
