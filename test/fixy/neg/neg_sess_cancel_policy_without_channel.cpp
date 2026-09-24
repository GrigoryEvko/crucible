// check::Cancel sends a cancellation to the peer from the destructor.
// This Resource has no cancel_session function, so it cannot send one,
// and a handle under check::Cancel over it is refused when the class is
// built.

#include <fixy/session/Handle.h>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct SilentWire {
    int last_sent = 0;
};
using Refused = s::SessionHandle<s::Send<Msg, s::End>, SilentWire, void, s::check::Cancel>;
}  // namespace

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
