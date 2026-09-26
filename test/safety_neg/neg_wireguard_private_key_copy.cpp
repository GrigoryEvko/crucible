// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A private key is classified and moves only.  A copy of it does not
// compile.

#include <crucible/cntp/_wip/Wireguard.h>

int main() {
    auto key = crucible::cntp::_wip::admit_wireguard_secret_key_b64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").value();
    auto copy = key;
    return static_cast<int>(copy.size());
}
