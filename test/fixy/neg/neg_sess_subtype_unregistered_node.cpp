// The relation reads every node of both spines.  A node that no header
// registers stops the build, and the message names it.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

template <class T, class K>
struct Teleport {};
using Odd = s::Send<int, Teleport<int, s::End>>;

}  // namespace

int main() {
    static_cast<void>(s::is_subtype_sync_v<Odd, s::Send<int, s::End>>);
    return 0;
}
