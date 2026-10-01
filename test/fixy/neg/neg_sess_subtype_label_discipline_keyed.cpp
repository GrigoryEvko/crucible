// A keyed Select puts label words on the wire, and a positional Select
// puts positions.  The two cannot face one peer, so a keyed choice never
// refines a positional one.

#include <fixy/session/Subtype.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_subtype_label_discipline_keyed_types {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
using Sub = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;
using Super = s::Select<s::Send<int, s::End>>;

}  // namespace neg_sess_subtype_label_discipline_keyed_types

using namespace neg_sess_subtype_label_discipline_keyed_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
