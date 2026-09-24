// An Offer with a Sender note and no branch is an empty choice: the note
// is not a branch, and the peer has no label to send.  The relation
// refuses it, also against itself.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Alice {};
using NoteOnly = s::Offer<s::Sender<Alice>>;

}  // namespace

int main() {
    s::assert_subtype_sync<NoteOnly, NoteOnly>();
    return 0;
}
