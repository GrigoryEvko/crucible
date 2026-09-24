// The mint refuses a Select whose label branches mix a keyed and a
// positional branch, because the protocol is not well-formed.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;

// The label types have external linkage: a label word is a stable id,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_mint_mixed_label_keys_types {
struct Bob {};
struct Hello {};
struct Wire {};
}  // namespace neg_sess_mint_mixed_label_keys_types

using namespace neg_sess_mint_mixed_label_keys_types;

using Mixed = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<int, s::End>>;

int main() {
    auto handle = s::mint_session_handle<Mixed, Wire>(Wire{});
    (void)handle;
    return 0;
}
