// A SealedRefined has no door that returns the value, so it does not
// drop its predicate to the bare payload.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

using Sealed = s::Send<::fixy::SealedRefined<::fixy::positive, int>, s::End>;
using Bare = s::Send<int, s::End>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sealed, Bare>();
    return 0;
}
