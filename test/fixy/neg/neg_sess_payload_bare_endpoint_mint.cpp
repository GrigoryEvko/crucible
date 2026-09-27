// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A plain session carries a live handle as a bare payload.  The handle
// sits inside a Loop and holds a region, so a hand-off could not
// delegate it, and a bare send would move the region with no set change.
// The mint refuses the protocol, because the permission flow does not
// close over a payload that the walk refuses.
//
// Expected diagnostic: the mint has no match, and the failed clause is
// the permission flow of the carrier protocol.
#include <fixy/session/Handle.h>

namespace neg_sess_payload_bare_endpoint_mint_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
using InLoop = ::fixy::session::SessionHandle<
    ::fixy::session::Send<int, ::fixy::session::Continue>, Wire,
    ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>, ::fixy::session::check::Enforced,
    ::foundation::permissions::PermSet<Region>>;
using Carrier = ::fixy::session::Send<InLoop, ::fixy::session::End>;
}  // namespace neg_sess_payload_bare_endpoint_mint_types

int main() {
    using namespace neg_sess_payload_bare_endpoint_mint_types;
    auto handle = ::fixy::session::mint_session_handle<Carrier, Wire>(Wire{});
    (void)handle;
    return 0;
}
