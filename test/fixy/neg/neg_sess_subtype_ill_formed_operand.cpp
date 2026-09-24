// An operand that is not well-formed relates to nothing, itself
// included.  An unguarded loop never stops its unfold.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

using Spin = s::Loop<s::Continue>;

}  // namespace

int main() {
    s::assert_subtype_sync<Spin, Spin>();
    return 0;
}
