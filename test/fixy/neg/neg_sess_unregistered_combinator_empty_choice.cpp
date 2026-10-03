// An is_empty_choice that answered false for every type it does not know
// would let an empty Select hidden under such a type pass the handle
// gate, and the handle would then get stuck at the choice.  The walk
// refuses the unknown node and names it.
//
// The constant below is the attack.  A walk that answered false for an
// unknown node would compile it.  An assertion on the constant gives a
// second error for the same cause, when the walk refuses the node.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

// A protocol node that no header registers, with a dead end below it.
template <class K>
struct Detour {};

}  // namespace

[[maybe_unused]] constexpr bool is_admitted = !s::is_empty_choice_v<s::Send<int, Detour<s::Select<>>>>;

int main() { return 0; }
