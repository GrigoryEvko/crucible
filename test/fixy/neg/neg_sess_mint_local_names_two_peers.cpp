// The projection of a three-role protocol onto R0 receives from R1 and
// then from R2.  A session mint has one carrier and no global type, so
// it cannot check that the protocol is implementable on that carrier.
// The mint refuses a local type that names two peers.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_mint_local_names_two_peers_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct Val {};
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_mint_local_names_two_peers_types

using namespace neg_sess_mint_local_names_two_peers_types;

using TwoSenders = g::Msg<R1, R0, Val, bool, g::Msg<R2, R0, Val, bool, g::End>>;
using ReceivesFromTwo = typename s::project_t<TwoSenders, R0>::local;

int main() {
    auto handle = s::mint_session_handle<ReceivesFromTwo, Wire>(Wire{});
    static_cast<void>(handle);
    return 0;
}
