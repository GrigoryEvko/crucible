// The Sender note of an Offer is compared for equality.  An Offer that
// names its sender does not stand for one that names no sender, because
// crash analysis reads the note.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Alice {};
using Noted = s::Offer<s::Sender<Alice>, s::Recv<int, s::End>>;
using Plain = s::Offer<s::Recv<int, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Noted, Plain>();
    return 0;
}
