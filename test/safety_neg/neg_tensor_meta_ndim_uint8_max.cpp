// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidNDim with the value UINT8_MAX in
// constexpr context — the wide-miss fixture for the TensorMeta::ndim
// deserialize cap.
//
// Per PROD-WRAP-5 (#534), ValidNDim is
// safety::Refined<safety::bounded_above<kMaxTensorNDim>, uint8_t>
// with kMaxTensorNDim == 8.  UINT8_MAX (0xFF) is over 31× the cap,
// well past any plausible tensor dimensionality (NumPy / PyTorch
// effective max is ~32; structural inline-array cap here is 8).
// Without the gate, an adversarial Cipher payload supplying ndim=255
// would (1) pass through read_meta unchecked, (2) hit a downstream
// `pre(meta.ndim <= 8)` contract violation in compute_storage_nbytes
// or trigger UB-via-[[assume]] under semantic=ignore on hot-path TUs,
// (3) overrun sizes[8] / strides[8] in any per-dim iteration before
// either downstream check fires.
//
// Companion fixture: neg_tensor_meta_ndim_above_max.cpp
//   * That one is the boundary edge (= kMaxTensorNDim + 1 = 9).
//   * This one is the upper-bound wide miss (= UINT8_MAX = 0xFF).
//     Catches "drop the bound entirely" regression where ValidNDim
//     degenerates from Refined<bounded_above<kMaxTensorNDim>> into a
//     plain uint8_t typedef.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/TensorMeta.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<kMaxTensorNDim>(v)`) to be exercised at compile
    // time.  v == UINT8_MAX → kMaxTensorNDim < UINT8_MAX →
    // predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidNDim bad{static_cast<uint8_t>(UINT8_MAX)};
    (void)bad;
    return 0;
}
