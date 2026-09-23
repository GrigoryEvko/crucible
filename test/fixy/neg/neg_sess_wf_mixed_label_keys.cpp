// One label branch of the Select names a label key and the other does
// not.  A keyed branch sends its label word and a positional branch sends
// its position, so the choice has no single kind of wire word, and the
// gate names the rule that the choice breaks.

#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>

namespace s = fixy::session;

namespace {
struct Bob {};
struct Hello {};
}  // namespace

using Mixed = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<int, s::End>>;

int main() {
    s::ensure_choices_well_formed<Mixed>();
    return 0;
}
