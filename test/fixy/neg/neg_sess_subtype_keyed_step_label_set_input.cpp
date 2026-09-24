// A keyed Recv is the Offer of its one branch.  An Offer of the labels L0
// and L1 does not receive the label L2 that the keyed Recv of the
// supertype receives, so it does not refine that step.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_subtype_keyed_step_label_set_input_types {

namespace s = ::fixy::session;

struct Bob {};
struct L0 {};
struct L1 {};
struct L2 {};
using Sub = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, L0, int>, s::End>, s::Recv<s::PeerMsg<Bob, L1, int>, s::End>>;
using Super = s::Recv<s::PeerMsg<Bob, L2, int>, s::End>;

}  // namespace neg_sess_subtype_keyed_step_label_set_input_types

using namespace neg_sess_subtype_keyed_step_label_set_input_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
