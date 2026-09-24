// The clash of one label word with two label keys, on an Offer.  The
// relation refuses it on an input choice as it does on an output choice.
// The stable id under the word refuses each closure label first, and the
// relation stays as the guard against a collision of two stable ids.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

// Bob has external linkage, so the stable id refuses the closure label
// and not the role.
namespace neg_sess_subtype_label_word_clash_input_types {

namespace s = ::fixy::session;

struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
using Sub = s::Offer<s::Recv<s::PeerMsg<Bob, First, int>, s::End>>;
using Super = s::Offer<s::Recv<s::PeerMsg<Bob, Second, int>, s::End>>;

}  // namespace neg_sess_subtype_label_word_clash_input_types

using namespace neg_sess_subtype_label_word_clash_input_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
