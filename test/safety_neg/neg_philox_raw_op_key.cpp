// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Philox-4 fixture #1: the raw op_key() API is deliberately not
// public.  Per-op keys must be minted through op_key_det(), preserving
// DetSafe<Pure> provenance until a caller explicitly consumes it.

#include <crucible/Philox.h>

int main() {
    auto key = crucible::Philox::op_key(
        1u, 2u, crucible::ContentHash{3u});
    (void)key;
    return 0;
}
