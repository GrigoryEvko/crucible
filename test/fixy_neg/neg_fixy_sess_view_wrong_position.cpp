// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsview::mint_session_view<AtRecv>(send_handle)` —
// the handle's compile-time Proto is `Send<Msg, End>`, NOT
// `Recv<...>`.  The mint factory's `requires HandleIsAt<Handle,
// Tag>` gate rejects.  This pins the §XXI mint discipline at the
// fixy:: re-export boundary: a future regression that softens
// the gate (e.g., to a bool flag or runtime check) would silently
// admit mismatched position requests, breaking the
// non-consuming-inspection invariant (observe metrics could
// claim "handle is at Recv" when it's actually at Send).
//
// FIXY-V-063 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_view_message_type_on_non_send_recv.cpp
//      (session_view_message_type rejects non-Send/Recv views)
//   3. neg_fixy_sess_view_branch_count_on_non_select_offer.cpp
//      (session_view_branch_count rejects non-Select/Offer views)
//
// This fixture's role: pin the HandleIsAt concept gate at the
// mint authorisation point.  Without it, observe/debug renderers
// could mint structurally-wrong views and report misleading
// protocol-position metrics during incident triage.

#include <crucible/fixy/SessView.h>

#include <utility>  // std::declval

namespace fsview = ::crucible::fixy::sess::view;
namespace proto  = ::crucible::safety::proto;

namespace v063_neg_alpha {
struct FakeResource {};
struct Msg {};
using SendProto = proto::Send<Msg, proto::End>;
using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;
}  // namespace v063_neg_alpha

// Should FAIL: handle is at Send, AtRecv is the wrong position.
// `decltype` invokes the function template in unevaluated context,
// which still requires constraint satisfaction.  GCC fires "no
// matching function for call to ...mint_session_view<AtRecv>"
// or "constraints not satisfied" naming HandleIsAt /
// handle_is_at_v.  Unevaluated context avoids needing an actual
// SessionHandle instance (whose Send-state destructor would fire
// abandonment-check at scope exit).
using BadView = decltype(fsview::mint_session_view<fsview::AtRecv>(
    std::declval<v063_neg_alpha::SendHandle const&>()));

int main() {
    (void) sizeof(BadView);  // force resolution
    return 0;
}
