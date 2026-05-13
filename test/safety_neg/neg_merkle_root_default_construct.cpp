// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidMerkleRoot with a default-initialized
// MerkleHash{} in constexpr context — the wide-miss / NSDMI mismatch
// fixture for the Tx::commit non-zero gate (and the
// computed_merkle_hash() accessor's `pre(is_non_zero(merkle_hash))`
// clause).
//
// Per #937 WRAP-MerkleDag-1, ValidMerkleRoot is
// safety::Refined<safety::non_zero, MerkleHash>.  MerkleHash{}
// reaches zero via the strong-id default ctor (CRUCIBLE_STRONG_HASH
// initializes the underlying uint64_t to 0 via NSDMI).  This is
// distinct from the explicit-literal `MerkleHash{0}` case because:
//
//   * The literal-zero fixture (neg_merkle_root_zero_literal.cpp)
//     catches a caller who explicitly synthesizes zero — an obviously-
//     buggy hand-written zero spelling.
//   * This default-construct fixture catches the silent path: a
//     `crucible::MerkleHash mh;` declaration, or a struct field whose
//     NSDMI has not been overridden, or an uninitialized return slot
//     that the compiler value-initializes to zero.  In production
//     this is the realistic mode — make_region(...) returns a
//     RegionNode whose merkle_hash is default-constructed (zero) and
//     stays zero until recompute_merkle runs.  A racing bg-thread
//     that commits before recompute_merkle would smuggle the default
//     value through; ValidMerkleRoot rejects it at construction.
//
// Two fixtures rather than one because HS14 mandates ≥2 negative-
// compile fixtures per new soundness gate, each demonstrating a
// distinct mismatch class.  Different zero-source classes (explicit
// literal vs default-construct via NSDMI) cover different drift modes
// — a future regression that admits the default value silently
// passes the literal fixture, but fails this one; vice versa for
// "drop the upper bound entirely".  Both fixtures together pin the
// gate.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.

#include <crucible/MerkleDag.h>
#include <crucible/Types.h>

int main() {
    // Default-init: MerkleHash{} fires CRUCIBLE_STRONG_HASH's NSDMI,
    // initializing value_ to 0.  ValidMerkleRoot's ctor therefore
    // calls `non_zero(MerkleHash{})` → `0 != 0` → false → contract
    // violation → not a constant expression → ill-formed.
    constexpr crucible::ValidMerkleRoot bad{crucible::MerkleHash{}};
    (void)bad;
    return 0;
}
