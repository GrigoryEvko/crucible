// A label branch of a keyed Select starts with a Loop.  The label word is
// the whole message of the branch, so the handle enters the branch past
// its label step.  The label step of this branch is also the entry of the
// loop, where it is a Select of its own, and the gate refuses the branch.

#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>

namespace s = fixy::session;

namespace {
struct Bob {};
struct Tick {};
struct Stop {};
}  // namespace

using LoopFirst = s::Select<s::Loop<s::Send<s::PeerMsg<Bob, Tick, int>, s::Continue>>,
                            s::Send<s::PeerMsg<Bob, Stop, int>, s::End>>;

int main() {
    s::ensure_choices_well_formed<LoopFirst>();
    return 0;
}
