// One label branch of the Select names a label key and the other does
// not.  A keyed branch sends its label word and a positional branch sends
// its position, so the choice has no single kind of wire word, and the
// gate names the rule that the choice breaks.

#include <fixy/session/Protocol.h>

namespace s = fixy::session;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_wf_mixed_label_keys_types {
struct Bob {};
struct Hello {};
}  // namespace neg_sess_wf_mixed_label_keys_types

using namespace neg_sess_wf_mixed_label_keys_types;

using Mixed = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<int, s::End>>;

int main() {
    s::ensure_choices_well_formed<Mixed>();
    return 0;
}
