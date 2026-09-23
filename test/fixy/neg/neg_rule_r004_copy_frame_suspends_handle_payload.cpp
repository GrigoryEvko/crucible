// R004: a suspension x a live session handle x a frame that is not
// linear.
//
// The second mismatch class: the binding states no session atom.  Its
// payload is a session handle at a Send, so the rule reads the live
// handle from the payload type.  The suspension is on Reentrancy, and
// the copy usage lets the continuation run twice, which would send the
// same message two times.  as_public keeps I004 out.
//
// The binding is named by its type only.  A session handle has no
// default constructor, so an object of this type cannot be built here.

#include <fixy/Fn.h>
#include <fixy/session/Handle.h>

struct wire final {
    int last_sent = 0;
};

using AtSend = ::fixy::session::SessionHandle<::fixy::session::Send<int, ::fixy::session::End>, wire>;
using Refused = ::fixy::fn<AtSend, ::fixy::atom::copy, ::fixy::atom::as_public, ::fixy::atom::coroutine>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
