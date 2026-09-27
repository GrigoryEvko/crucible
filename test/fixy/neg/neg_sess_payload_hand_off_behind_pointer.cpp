// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A payload points at a DelegatedSession.  The hand-off stays with the
// sender, so the recipient would hold a second name for the endpoint and
// its tokens.  A hand-off travels by value, one time.
//
// Expected diagnostic: the payload is refused, and the reason names a
// DelegatedSession reached through a pointer.
#include <fixy/session/Delegate.h>

#include <memory>

namespace neg_sess_payload_hand_off_behind_pointer_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
using Handed = ::fixy::session::DelegatedSession<::fixy::session::End, Wire, ::fixy::session::DefaultAbandonmentPolicy,
                                                 ::foundation::permissions::EmptyPermSet>;
}  // namespace neg_sess_payload_hand_off_behind_pointer_types

int main() {
    using Delta =
        ::fixy::session::payload_perm_delta<std::unique_ptr<neg_sess_payload_hand_off_behind_pointer_types::Handed>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
