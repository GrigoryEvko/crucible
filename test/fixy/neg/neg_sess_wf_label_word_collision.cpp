// Two closure types are distinct types that print one name, so their
// label keys have one label word.  The peer dispatches on the word and
// could not tell the two labels apart, so the choice is refused.

#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>

namespace s = fixy::session;

namespace {
struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
}  // namespace

using SameWord = s::Select<s::Send<s::PeerMsg<Bob, First, int>, s::End>, s::Send<s::PeerMsg<Bob, Second, int>, s::End>>;

int main() {
    s::ensure_choices_well_formed<SameWord>();
    return 0;
}
