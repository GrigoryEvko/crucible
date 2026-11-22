// Negative compile test.  This file must fail to compile.
//
// A crash send gives a payload to a crashed peer back in `undelivered`.
// The payload can be a token, which is linear.  An assignment over
// `undelivered` would end the token that it holds with no door, so the
// token would be lost.  The base Option has no assignment for a linear
// payload.
//
// Expected diagnostic: the deleted assignment of the Option, which names
// the payload that an assignment drops.
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Flow.h>

namespace s = ::fixy::session;

namespace neg_sess_crash_undelivered_token_assigned_types {
struct TokenTag {};
struct TokenBrand {};
struct NextHandle {};
using Token = s::Transferable<int, TokenTag, TokenBrand>;
}  // namespace neg_sess_crash_undelivered_token_assigned_types

int main() {
    using namespace neg_sess_crash_undelivered_token_assigned_types;
    s::CrashSend<NextHandle, Token> sent{};
    sent.undelivered = ::fixy::Option<Token>{};
    static_cast<void>(sent);
    return 0;
}
