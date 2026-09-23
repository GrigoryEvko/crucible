// I004: as_secret x a suspension on ControlFlow x a session protocol,
// without constant time.
//
// The second mismatch class: the classification is stated as the top of
// the lattice, and the suspension is written on the other axis.  The
// pack trips I004 alone.

#include <fixy/Fn.h>

struct handshake final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::as_secret,
                                ::fixy::atom::ctrl::coroutine<::fixy::atom::ctrl::async_task>,
                                ::fixy::atom::protocol<handshake>> refused{};
    return 0;
}
