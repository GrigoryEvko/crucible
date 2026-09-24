// Two branches of one Select name the same peer and label, with
// different payloads.  A label names one branch of a choice, as in a
// global type, so the protocol is not well-formed and no handle is
// minted for it.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

namespace s = fixy::session;

// The label types have external linkage: a label word is a stable id,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_wf_repeated_label_types {
struct Bob {};
struct Hello {};
struct Wire {};
}  // namespace neg_sess_wf_repeated_label_types

using namespace neg_sess_wf_repeated_label_types;

using SameLabel = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<s::PeerMsg<Bob, Hello, bool>, s::End>>;

int main() {
    auto handle = s::mint_session_handle<SameLabel, Wire>(Wire{});
    (void)handle;
    return 0;
}
