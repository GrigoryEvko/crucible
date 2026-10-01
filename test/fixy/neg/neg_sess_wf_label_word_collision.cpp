// Two closure types are distinct types that print one name.  The stable
// id under a label word refuses a closure, because its printed name is
// not an identity.  The compiler still reaches the choice check.  That
// check refuses two label keys of one word, and it stays as the guard
// against a collision of two stable ids.

#include <fixy/session/Protocol.h>

namespace s = fixy::session;

// Bob has external linkage, so the stable id refuses the closure label
// and not the role.
namespace neg_sess_wf_label_word_collision_types {
struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
}  // namespace neg_sess_wf_label_word_collision_types

using namespace neg_sess_wf_label_word_collision_types;

using SameWord = s::Select<s::Send<s::PeerMsg<Bob, First, int>, s::End>, s::Send<s::PeerMsg<Bob, Second, int>, s::End>>;

int main() {
    s::ensure_choices_well_formed<SameWord>();
    return 0;
}
