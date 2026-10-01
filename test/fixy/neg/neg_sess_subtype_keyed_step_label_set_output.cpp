// A keyed Send is the Select of its one branch.  It sends the label L2,
// which the Select of the supertype, of the labels L0 and L1, does not
// send, so it does not refine that Select.

#include <fixy/session/Subtype.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_subtype_keyed_step_label_set_output_types {

namespace s = ::fixy::session;

struct Bob {};
struct L0 {};
struct L1 {};
struct L2 {};
using Sub = s::Send<s::PeerMsg<Bob, L2, int>, s::End>;
using Super = s::Select<s::Send<s::PeerMsg<Bob, L0, int>, s::End>, s::Send<s::PeerMsg<Bob, L1, int>, s::End>>;

}  // namespace neg_sess_subtype_keyed_step_label_set_output_types

using namespace neg_sess_subtype_keyed_step_label_set_output_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
