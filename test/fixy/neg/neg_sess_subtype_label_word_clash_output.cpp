// Two closure types in one translation unit print one name, so a stable
// id cannot name a closure label.  The member check of the message node
// refuses a closure label before the relation runs, and the build stops at
// that refusal.  The relation keeps its refusal of two label keys of one
// word, as the guard against a collision of two stable ids.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

// Bob has external linkage, so the closure label is the one part of the
// message that a stable id cannot name.
namespace neg_sess_subtype_label_word_clash_output_types {

namespace s = ::fixy::session;

struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
using Sub = s::Select<s::Send<s::PeerMsg<Bob, First, int>, s::End>>;
using Super = s::Select<s::Send<s::PeerMsg<Bob, Second, int>, s::End>>;

}  // namespace neg_sess_subtype_label_word_clash_output_types

using namespace neg_sess_subtype_label_word_clash_output_types;

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
