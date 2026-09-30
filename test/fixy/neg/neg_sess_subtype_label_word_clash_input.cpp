// The clash of one label word with two label keys, on an Offer.  The
// member check of the message node refuses each closure label first, on an
// input choice as on an output choice, and the build stops at that
// refusal.  The relation stays as the guard against a collision of two
// stable ids.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

// Bob has external linkage, so the closure label is the one part of the
// message that a stable id cannot name.
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
