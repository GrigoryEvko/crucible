// A value tagged External is untrusted input.  It does not drop to the
// bare payload, because that would carry it into a position that
// assumes validation.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

using Untrusted = s::Send<::fixy::Tagged<int, ::fixy::tags::source::External>, s::End>;
using Bare = s::Send<int, s::End>;

}  // namespace

int main() {
    s::assert_subtype_sync<Untrusted, Bare>();
    return 0;
}
