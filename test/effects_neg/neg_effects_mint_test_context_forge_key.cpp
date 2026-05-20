// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #1 for effects::mint_test_context
// (Capabilities.h:438).  The only path to a Test context is
// `mint_test_context(detail::ctx_mint::test_key)`.  The passkey
// `test_key` has a PRIVATE default ctor, friended ONLY to
// effects::testing::TestWitness (the narrowest friend set of the
// three context passkeys — Test contexts must originate from the
// test-witness scaffold, never production).  An unprivileged TU
// that forges a key via `test_key{}` must fail to compile.
//
// Distinct mismatch class from
// neg_effects_mint_test_context_keyless.cpp (#2): there the factory
// is called with NO key (overload resolution fails); here a key IS
// named but cannot be CONSTRUCTED (private default ctor).
//
// Expected diagnostic: "is private within this context" /
// "private member" / "test_key".

#include <crucible/effects/Capabilities.h>

namespace eff = crucible::effects;

int main() {
    auto t = eff::mint_test_context(eff::detail::ctx_mint::test_key{});
    (void)t;
    return 0;
}
