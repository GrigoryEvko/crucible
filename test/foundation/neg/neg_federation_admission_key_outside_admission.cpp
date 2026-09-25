// The admission key is the one route to a federation peer token, and
// only an admission makes it, after a handshake verifies.  Code outside
// the admission cannot build the key, so it cannot build the token.

#include <foundation/permissions/Permission.h>

namespace fp = ::foundation::permissions;

namespace {
struct PeerOrg {};
struct PeerBrand {};
}  // namespace

int main() {
    [[maybe_unused]] fp::Permission<fp::tag::FederatedPeer<PeerOrg>, PeerBrand> token{fp::federation_admission_key{}};
    return 0;
}
