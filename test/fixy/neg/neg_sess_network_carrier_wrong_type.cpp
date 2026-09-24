// A carrier declares session_network with an integer.  An integer with
// that name could be a count, so the declaration states nothing, and the
// gate refuses the binding with the reason that names the type.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_carrier_wrong_type_types {
struct R0 {};
struct R1 {};
struct Val {};
struct CountedQueue {
    static constexpr unsigned session_network = 0;
};
}  // namespace neg_sess_network_carrier_wrong_type_types

using namespace neg_sess_network_carrier_wrong_type_types;

using OneMessage = g::Msg<R0, R1, Val, int, g::End>;

int main() {
    s::ensure_carrier_implements<OneMessage, CountedQueue>();
    return 0;
}
