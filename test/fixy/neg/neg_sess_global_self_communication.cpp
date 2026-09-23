// A role cannot send a message to itself.  The projection of such a
// transmission puts the same role at the send and at the receive, and
// the role then waits for a message that only it can send.
//
// Each other clause of well-formedness holds here: the transmission has
// one branch, the label is unique, and there is no recursion.  So the
// refusal can only come from the self-communication clause.

#include <fixy/session/Global.h>

namespace {

namespace g = ::fixy::session::global;

struct Alice {};
struct Ping {};

using SelfPing = g::Msg<Alice, Alice, Ping, int, g::End>;

constexpr int declare_protocol() noexcept {
    g::ensure_global_well_formed<SelfPing>();
    return 0;
}

}  // namespace

int main() { return declare_protocol(); }
