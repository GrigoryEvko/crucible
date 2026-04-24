// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `ensure_dual<P1, P2>()` invoked on a non-dual pair —
// the client expects `Recv<Response, End>` after sending Request, but
// the server reads two Recv's in sequence (forgot to switch to Send
// for Response).  Per #431 the static_assert fires
// `[Dual_Mismatch]`, naming the framework's deadlock-freedom
// guarantee that fails when endpoints aren't dual.

#include <crucible/safety/Session.h>

using namespace crucible::safety::proto;

struct Request {};
struct Response {};

using ClientProto = Send<Request, Recv<Response, End>>;
// Bug: ServerProto should be Recv<Request, Send<Response, End>>;
// the user wrote Recv<Request, Recv<Response, End>> by mistake.
using ServerProto = Recv<Request, Recv<Response, End>>;

void compile_time_reject() {
    ensure_dual<ClientProto, ServerProto>();
}

int main() { return 0; }
