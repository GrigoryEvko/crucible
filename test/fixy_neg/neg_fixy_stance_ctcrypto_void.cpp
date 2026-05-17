// fixy_neg: stance::CtCrypto<void> rejects via Type-axis gate.
//
// HS14 floor for FIXY-AUDIT-D4 (CtCrypto stance, Type-axis angle).
// Existing fixtures pin CtCrypto's axis discipline:
//   - neg_fixy_ctcrypto_with_io       — hand-rolled IO violates §30.14
//   - neg_fixy_ctcrypto_unengaged_security — drop as_secret engagement
// This fixture adds the Type-axis angle: `void` is not an object,
// so IsAccepted rejects before any of the constant-time-discipline
// axes are evaluated.
//
// Expected diagnostic: "IsAccepted".

#include <crucible/fixy/Fn.h>

namespace stance = crucible::fixy::stance;

using BadCtCryptoVoid = stance::CtCrypto<void>;

static_assert(sizeof(BadCtCryptoVoid) > 0,
    "instantiate stance::CtCrypto<void> to force the Type-axis "
    "rejection (void is not an object).");

int main() { return 0; }
