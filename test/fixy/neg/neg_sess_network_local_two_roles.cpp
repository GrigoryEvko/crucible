// A carrier that states the Local network has no peer, so a choice over
// it puts no label on a wire.  This global type sends one message from R0
// to R1, and a Local carrier cannot carry it, because R1 would read a
// label that R0 never writes.  The carrier gate refuses the binding.

#include <fixy/session/Network.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_network_local_two_roles_types {
struct R0 {};
struct R1 {};
struct Hello {};
struct Cell {
    static constexpr s::Network session_network = s::Network::Local;
};
}  // namespace neg_sess_network_local_two_roles_types

using namespace neg_sess_network_local_two_roles_types;

using OneMessage = g::Comm<R0, R1, g::Branch<Hello, int, g::End>>;

int main() {
    s::ensure_carrier_implements<OneMessage, Cell>();
    return 0;
}
