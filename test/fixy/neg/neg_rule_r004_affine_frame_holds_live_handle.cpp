// R004: a suspension x a live session handle x a frame that is not
// linear.
//
// The frame states that it holds a live handle, suspends on ControlFlow,
// and states the affine usage, so the continuation may be dropped.  A
// dropped continuation drops the protocol that the handle owes.  The
// pack states as_public, so I004 stands down and the pack trips R004
// alone.

#include <fixy/Fn.h>

struct handshake final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::affine, ::fixy::atom::as_public,
                                ::fixy::atom::ctrl::coroutine<::fixy::atom::ctrl::async_task>,
                                ::fixy::atom::session::live_handle<handshake>> refused{};
    return 0;
}
