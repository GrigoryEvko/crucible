// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The passkey of the background context, built from one byte.  The key
// is the only door to mint_bg_context.  Each constructor of the key is
// user-provided, so the key is not trivially copyable, and std::bit_cast
// has no candidate.

#include <crucible/effects/_Capabilities.h>

#include <bit>

namespace eff = ::crucible::effects;

int main() {
    auto forged = std::bit_cast<eff::detail::ctx_mint::bg_key>(char{0});
    (void)forged;
    return 0;
}
