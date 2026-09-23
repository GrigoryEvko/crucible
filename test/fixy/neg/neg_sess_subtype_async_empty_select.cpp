// The bounded asynchronous check refuses an empty Select too, at every
// capacity, because it refuses an operand that is not well-formed.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
using Empty = s::Select<>;
using One = s::Select<s::Send<PingReq, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_async<Empty, One, 4>();
    return 0;
}
