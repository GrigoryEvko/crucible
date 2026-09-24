// W003: hot x a payload that is a live session handle x no stated wait.
//
// The second door to the same premise: the binding states no session
// atom, and its payload is a session handle at a Recv.  The receive waits
// in the transport for the peer's message.  The rule reads the handle
// from the payload through the Stepping contract, as R004 does, so the
// binding does not have to repeat it in its pack.
//
// The binding is named by its type only.  A session handle has no
// default constructor, so an object of this type cannot be built here.
// The cost and refinement atoms silence H001 and H002, so the pack trips
// W003 alone.

#include <fixy/Fn.h>
#include <fixy/session/Handle.h>

struct wire final {
    int last_received = 0;
};

struct ring_depth_proved final {};

using AtRecv = ::fixy::session::SessionHandle<::fixy::session::Recv<int, ::fixy::session::End>, wire>;
using Refused = ::fixy::fn<AtRecv, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                           ::fixy::atom::refined_with<ring_depth_proved>>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
