// Send is covariant in its payload.  A sender of bare ints cannot stand
// where the receiver is entitled to a positive value, because the sender
// has nothing to prove the predicate with.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

using Bare = s::Send<int, s::End>;
using Promised = s::Send<::fixy::Refined<::fixy::positive, int>, s::End>;

}  // namespace

int main() {
    s::assert_subtype_sync<Bare, Promised>();
    return 0;
}
