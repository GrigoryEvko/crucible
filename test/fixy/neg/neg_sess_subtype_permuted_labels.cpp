// A positional Select with the same branches in another order is another
// protocol, because the handle sends the position of a branch as its wire
// label.  The relation compares the branches of a positional choice
// position by position, and names the first pair of payloads that differ.
// A keyed choice sends a label word, so its branches pair by label and a
// permutation of them is the same protocol.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Hello {};
struct Bye {};
using Written = s::Select<s::Send<Hello, s::End>, s::Send<Bye, s::End>>;
using Permuted = s::Select<s::Send<Bye, s::End>, s::Send<Hello, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Permuted, Written>();
    return 0;
}
