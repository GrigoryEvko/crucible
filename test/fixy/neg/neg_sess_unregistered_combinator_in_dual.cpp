// The dual of a protocol reads every node on its spine.  A node that no
// header registers stops the build at that node, and the message names
// it, so the refusal points at the combinator that is missing.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

template <class T, class K>
struct Teleport {};

}  // namespace

int main() { return sizeof(s::dual_of<s::Send<int, Teleport<int, s::End>>>) == 0 ? 1 : 0; }
