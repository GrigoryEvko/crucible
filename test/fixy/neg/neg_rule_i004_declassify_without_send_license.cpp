// I004: a declassification whose policy does not license IO x a
// suspension x a session protocol.
//
// HashForCompare releases a hash of the value and names no channel.  The
// send of the session is an export through IO, so the binding stays
// classified on the channel of the send.  The pack names no IO row and no
// Bg row, so no corpus entry matches, and it trips I004 alone.

#include <fixy/Fn.h>

struct handshake final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::HashForCompare>,
                                ::fixy::atom::coroutine, ::fixy::atom::protocol<handshake>> refused{};
    return 0;
}
