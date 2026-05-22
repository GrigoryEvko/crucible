// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `session_view_message_type_t<AtSelect view>` — the
// metafunction is specialised ONLY for AtSend / AtRecv views; the
// primary template `session_view_message_type<View>` is forward-
// declared without a `type` member, so instantiation on an
// AtSelect view (which has no single message payload — Select has
// branches, not a payload) fires "no type named 'type'" or
// "use of undefined struct".  This pins the per-tag specialisation
// discipline at the fixy:: re-export boundary: a future regression
// that adds a fallback returning `void` (or `nullptr_t`) would
// silently admit nonsense queries, hiding programmer errors at the
// observe/metrics layer.
//
// FIXY-V-063 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_view_wrong_position.cpp
//      (mint_session_view HandleIsAt gate)
//   3. neg_fixy_sess_view_branch_count_on_non_select_offer.cpp
//      (session_view_branch_count rejects non-Select/Offer)
//
// This fixture's role: pin the AtSend/AtRecv-only specialisation
// of session_view_message_type.  Without the forward-only primary,
// observability code asking "what type is being sent here?" on
// a Select view would compile but extract `void`, masking real
// protocol-shape bugs.

#include <crucible/fixy/SessView.h>

namespace fsview = ::crucible::fixy::sess::view;
namespace proto  = ::crucible::safety::proto;
namespace saf    = ::crucible::safety;

namespace v063_neg_beta {
struct FakeResource {};
struct Msg {};
using SendProto   = proto::Send<Msg, proto::End>;
using RecvProto   = proto::Recv<Msg, proto::End>;
using SelectProto = proto::Select<SendProto, RecvProto>;
using SelectHandle = proto::SessionHandle<SelectProto, FakeResource, void>;
using SelectView   = saf::ScopedView<SelectHandle, fsview::AtSelect>;
}  // namespace v063_neg_beta

int main() {
    // Should FAIL: session_view_message_type has no specialisation
    // for AtSelect views — primary template has no `type` member.
    // GCC fires "no type named 'type' in struct ...
    // session_view_message_type<...>" or "use of undefined".
    using BadMsg = fsview::session_view_message_type_t<
        v063_neg_beta::SelectView>;
    (void) sizeof(BadMsg);  // force instantiation
    return 0;
}
