// The callback entry point takes the same gate as the plain mint.  The
// local type sends to R1 and then to R2, so one carrier would take the
// traffic of two peers, and the entry point refuses it.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;
namespace g = fixy::session::global;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_with_session_local_names_two_peers_types {
struct R0 {};
struct R1 {};
struct R2 {};
struct Val {};
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_with_session_local_names_two_peers_types

using namespace neg_sess_with_session_local_names_two_peers_types;

using FanOut = g::Msg<R0, R1, Val, int, g::Msg<R0, R2, Val, int, g::End>>;
using SendsToTwo = typename s::project_t<FanOut, R0>::local;

int main() {
    Wire back = s::with_session<SendsToTwo, Wire>(Wire{}, [](auto head) noexcept { return head; });
    static_cast<void>(back);
    return 0;
}
