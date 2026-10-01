// A keyed Offer of the supertype receives a label that the Offer of the
// subtype does not receive.  The subtype must receive each label of the
// supertype, wherever it stands.

#include <fixy/session/Subtype.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_subtype_label_set_input_types {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
struct Bye {};
using Sub = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;
using Super =
    s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace neg_sess_subtype_label_set_input_types

using namespace neg_sess_subtype_label_set_input_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
