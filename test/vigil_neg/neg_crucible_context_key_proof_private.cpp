// Negative-compile test.  This file must fail to compile.
//
// Code that is no holder of a key of a context builds the proof of the brand,
// and mints a key with mint_held_key.  With that key it could open the context
// of a Vigil.  The constructor of the proof is private, and its friends are the
// Vigil and mint_context_key.

#include <crucible/CrucibleContext.h>

int main() {
    auto key = ::foundation::core::mint_held_key<crucible::ContextKeyBrand>(crucible::ContextKeyBrand::key_proof{});
    static_cast<void>(key);
    return 0;
}
