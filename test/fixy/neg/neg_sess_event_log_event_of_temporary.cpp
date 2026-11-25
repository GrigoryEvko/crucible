// Negative compile test.  This file must fail to compile.
//
// An event of a temporary session event log ends with the full expression, so a
// temporary log gives no event.
//
// Expected diagnostic: the overload of the subscript for a temporary log is
// deleted, with the reason.

#include <fixy/session/EventLog.h>

int main() {
    namespace s = ::fixy::session;
    return s::SessionEventLog{}[0].op() == s::SessionOp::Send ? 0 : 1;
}
