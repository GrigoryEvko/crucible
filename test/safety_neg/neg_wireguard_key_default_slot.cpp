// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A declared public key has no default.  An empty slot would hold a key
// that the key text rule never saw.

#include <crucible/cntp/_wip/Wireguard.h>

int main() {
    namespace wg = crucible::cntp::_wip;
    const wg::DeclaredWireguardPublicKey key{};
    return key.value().value()[0];
}
