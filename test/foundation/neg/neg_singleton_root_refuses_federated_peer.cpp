// A token for a federation peer comes only from a handshake that
// verifies.  The singleton root mint refuses a federation peer tag, as the
// plain root mint does, so no call site mints a peer token from nothing.
// The note names the concept that refuses the tag.

#include <foundation/permissions/Permission.h>

namespace {
struct PeerOrg {};
}  // namespace

int main() {
    [[maybe_unused]] auto token =
        ::foundation::permissions::mint_singleton_root<::foundation::permissions::tag::FederatedPeer<PeerOrg>>();
    return 0;
}
