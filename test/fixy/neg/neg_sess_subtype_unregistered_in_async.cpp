// The asynchronous check reads every node too, so an unknown node is
// refused there as it is in the synchronous relation.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

template <class K>
struct Detour {};
using Odd = s::Send<int, Detour<s::End>>;

}  // namespace

int main() {
    static_cast<void>(s::is_subtype_async_v<Odd, s::Send<int, s::End>, 2>);
    return 0;
}
