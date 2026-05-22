// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `session_view_branch_count_v<AtSend view>` — the
// metafunction is specialised ONLY for AtSelect / AtOffer views;
// the primary template `session_view_branch_count<View>` is
// forward-declared without a `value` member, so instantiation on
// an AtSend view (which has a payload, not branches) fires
// "no member named 'value'" / "incomplete type" / "use of
// undefined".  This pins the per-tag specialisation discipline at
// the fixy:: re-export boundary: a future regression that adds a
// fallback returning `0` would silently admit nonsense queries,
// hiding programmer errors at the observe/metrics layer.
//
// FIXY-V-063 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_view_wrong_position.cpp
//      (mint_session_view HandleIsAt gate)
//   2. neg_fixy_sess_view_message_type_on_non_send_recv.cpp
//      (session_view_message_type rejects non-Send/Recv)
//
// This fixture's role: pin the AtSelect/AtOffer-only specialisation
// of session_view_branch_count.  Without the forward-only primary,
// observability code asking "how many branches does this present?"
// on a Send view would compile but report 0, masking real
// protocol-shape bugs in fan-out / fan-in routing.

#include <crucible/fixy/SessView.h>

namespace fsview = ::crucible::fixy::sess::view;
namespace proto  = ::crucible::safety::proto;
namespace saf    = ::crucible::safety;

namespace v063_neg_gamma {
struct FakeResource {};
struct Msg {};
using SendProto  = proto::Send<Msg, proto::End>;
using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;
using SendView   = saf::ScopedView<SendHandle, fsview::AtSend>;
}  // namespace v063_neg_gamma

int main() {
    // Should FAIL: session_view_branch_count has no specialisation
    // for AtSend views — primary template has no `value` member.
    // GCC fires "no member named 'value' in struct ...
    // session_view_branch_count<...>" or "incomplete type" /
    // "use of undefined".
    constexpr auto bad = fsview::session_view_branch_count_v<
        v063_neg_gamma::SendView>;
    (void) bad;
    return 0;
}
