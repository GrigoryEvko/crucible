// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidNDim with the value
// kMaxTensorNDim + 1 in constexpr context — the boundary edge fixture
// for the TensorMeta::ndim deserialize cap.
//
// Per PROD-WRAP-5 (#534), ValidNDim is
// safety::Refined<safety::bounded_above<kMaxTensorNDim>, uint8_t>
// with kMaxTensorNDim == 8 (the inline sizes[8]/strides[8] structural
// limit on TensorMeta).  Values in [9, 255] are corrupt or
// adversarial — the deserialize path read_meta() in Serialize.h was
// previously unguarded, so any byte in that range slipped through to
// downstream consumers (compute_storage_nbytes, DimHash SIMD,
// per-dimension iteration over sizes/strides) where the structural
// invariant `meta.ndim <= 8` is silently assumed.
//
// Companion fixture: neg_tensor_meta_ndim_uint8_max.cpp
//   * This one is the boundary edge (= kMaxTensorNDim + 1 = 9).
//     Catches drift where the bound widens from
//     `bounded_above<kMaxTensorNDim>` to
//     `bounded_above<kMaxTensorNDim + K>` for any K >= 1, silently
//     admitting an ndim that overruns sizes[8] / strides[8].
//   * That one is the upper-bound wide miss (= UINT8_MAX = 0xFF).
//     Catches "drop the bound entirely" regression.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<kMaxTensorNDim>(v)`) to be exercised at compile
    // time.  v == kMaxTensorNDim + 1 → predicate(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidNDim bad{
        static_cast<uint8_t>(crucible::kMaxTensorNDim + 1u)};
    (void)bad;
    return 0;
}
