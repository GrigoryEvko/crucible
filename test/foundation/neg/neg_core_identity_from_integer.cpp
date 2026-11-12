// An identity comes only from mint_identity().  An identity from an integer
// could be equal to the identity of a different brand instance, so the
// constructor from a value is private.

#include <foundation/core/Identity.h>

int main() {
    ::foundation::core::Identity const minted = ::foundation::core::mint_identity();
    ::foundation::core::Identity const forged{1};
    return minted == forged ? 0 : 1;
}
