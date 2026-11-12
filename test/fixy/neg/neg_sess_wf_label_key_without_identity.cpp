// Two closure types are distinct types that print one name, so a stable id
// refuses a closure, and a closure label has no label word.  The choice
// check refuses a label key with no stable identity before it reads a
// word, so the build stops at that one refusal.  The check of two label
// keys of one word stays as the guard against a collision of two stable
// ids.

#include <fixy/session/Protocol.h>

namespace s = fixy::session;

// Bob has external linkage, so the stable id refuses the closure label
// and not the role.
namespace neg_sess_wf_label_key_without_identity_types {
struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
}  // namespace neg_sess_wf_label_key_without_identity_types

using namespace neg_sess_wf_label_key_without_identity_types;

using ClosureLabels =
    s::Select<s::Send<s::PeerMsg<Bob, First, int>, s::End>, s::Send<s::PeerMsg<Bob, Second, int>, s::End>>;

int main() {
    s::ensure_choices_well_formed<ClosureLabels>();
    return 0;
}
