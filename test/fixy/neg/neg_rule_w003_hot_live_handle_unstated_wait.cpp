// W003: hot x a live session handle x no stated wait.
//
// The frame holds a handle that still owes its protocol, so its next step
// goes through a transport: a receive waits for a message, and a send
// waits for room.  The binding does not say how it waits.  A hot path
// admits only a wait that stays in user space, and an unstated wait can
// be a futex, so the rule refuses it.  W001 refuses a stated kernel wait,
// and W003 refuses the wait that nobody stated.
//
// The cost and refinement atoms silence H001 and H002, so the pack trips
// W003 alone.

#include <fixy/Fn.h>

// The tags have external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct handshake final {};
struct ring_depth_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<fixture::ring_depth_proved>,
                                ::fixy::atom::session::live_handle<fixture::handshake>> refused{};
    return 0;
}
