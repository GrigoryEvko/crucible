// A positional Offer against a keyed Offer.  The peer of the keyed Offer
// sends label words, and the positional Offer dispatches on positions,
// so the relation refuses the pair in this direction too.

#include <fixy/session/Subtype.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_subtype_label_discipline_positional_types {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
using Sub = s::Offer<s::Recv<int, s::End>>;
using Super = s::Offer<s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace neg_sess_subtype_label_discipline_positional_types

using namespace neg_sess_subtype_label_discipline_positional_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
