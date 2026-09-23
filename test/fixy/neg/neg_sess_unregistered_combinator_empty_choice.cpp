// is_empty_choice answered false for every type the old primary did not
// know.  An empty Select hidden under such a type passed the handle
// gate, and the handle then got stuck at the choice.  The walk now
// refuses the unknown node and names it.
//
// The assertion below held before the repair.  It is the attack.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

// A protocol node that no header registers, with a dead end below it.
template <class K>
struct Detour {};

}  // namespace

static_assert(!s::is_empty_choice_v<s::Send<int, Detour<s::Select<>>>>);

int main() { return 0; }
