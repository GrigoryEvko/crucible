// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Philox-4 fixture #2: op_key_det() returns DetSafe<Pure,
// uint64_t>, not a raw integer.  A caller must make the provenance
// erasure explicit with .peek() / consume() at the boundary that owns it.

#include <crucible/Philox.h>

#include <cstdint>

int main() {
    std::uint64_t key = crucible::Philox::op_key_det(
        1u, 2u, crucible::ContentHash{3u});
    (void)key;
    return 0;
}
