// I004: classified x a suspension x a session protocol, without
// constant time.
//
// The binding names no Security atom, so it is classified by the strict
// pole.  The peer sees each send, and the time of each send follows the
// data.  The pack names no borrow and no Bg row, and trips I004 alone.

#include <fixy/Fn.h>

struct handshake final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::coroutine, ::fixy::atom::protocol<handshake>> refused{};
    return 0;
}
