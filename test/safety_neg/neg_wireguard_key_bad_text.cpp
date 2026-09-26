// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A public key is 44 characters of base64 that end in one padding
// character.  The checked mint of the key type refuses a text of zeros in a
// constant evaluation.

#include <crucible/cntp/_wip/Wireguard.h>

namespace wg = crucible::cntp::_wip;

constexpr wg::WireguardKeyB64 bad_key = ::fixy::mint_refined<wg::wireguard_key_text>(wg::WireguardKeyChars{});

int main() { return bad_key.value()[0]; }
