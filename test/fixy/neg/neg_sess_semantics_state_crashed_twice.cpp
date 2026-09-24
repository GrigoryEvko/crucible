// The set of crashed roles of a state lists each role once.  A list that
// names one role twice is no set, so the alias refuses the state.

#include <fixy/session/Semantics.h>

namespace {

struct P {};
struct Q {};
struct M {};

namespace g = ::fixy::session::global;

using Twice = g::State<g::Roles<P, P>, g::Msg<Q, P, M, int, g::End>>;
using Next = g::state_step_t<Twice, g::SendAction<Q, P, M, int>, ::fixy::session::ReliableSet<>>;

}  // namespace

int main() { return 0; }
