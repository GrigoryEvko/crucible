// Two closure types in one translation unit print one name.  The stable
// id under a label word refuses a closure, because its printed name is
// not an identity.  The compiler still reaches the relation.  The
// relation refuses two label keys of one word, and it stays as the guard
// against a collision of two stable ids.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

// Bob has external linkage, so the stable id refuses the closure label
// and not the role.
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
