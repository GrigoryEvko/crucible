// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A payload holds a live session endpoint by value, outside
// DelegatedSession.  The endpoint would move its permission set with no
// set change, and the rules of fixy/session/Delegate.h would not run.
// The payload walk refuses the endpoint.
//
// Expected diagnostic: the payload is refused, and the reason names a
// session endpoint outside DelegatedSession.
#include <fixy/session/Handle.h>

namespace neg_sess_payload_bare_endpoint_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
using Endpoint = ::fixy::session::SessionHandle<::fixy::session::Recv<int, ::fixy::session::End>, Wire, void,
                                                ::fixy::session::check::Enforced,
                                                ::foundation::permissions::PermSet<Region>>;
struct Envelope {
    int sequence = 0;
    Endpoint endpoint;
};
}  // namespace neg_sess_payload_bare_endpoint_types

int main() {
    using Delta = ::fixy::session::payload_perm_delta<neg_sess_payload_bare_endpoint_types::Envelope>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
