// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1022 WRAP-StorageNbytes-5
// (compute_storage_nbytes* TensorMeta input -> ExternalTensorMeta).
//
// Premise: storage-span computation is an adversarial boundary over
// TensorMeta values loaded from foreign traces / Vessel / disk.  A raw
// TensorMeta must not flow directly into the byte-span computation;
// callers must explicitly cross the provenance gate with
// external_tensor_meta(meta).
//
// Distinct mismatch class from neg_storage_nbytes_cross_tag.cpp:
//   * This fixture: raw TensorMeta rejected because Tagged's ctor is
//     explicit and the API demands ExternalTensorMeta.
//   * Companion: another Tagged provenance is rejected because
//     Tagged<T, Sanitized> is not Tagged<T, External>.

#include <crucible/ir001/MerkleDag.h>

int main() {
  crucible::TensorMeta meta{};
  meta.ndim = 1;
  meta.sizes[0] = ::crucible::tensor_dim(8);
  meta.strides[0] = ::crucible::tensor_dim(1);
  meta.dtype = crucible::ScalarType::Float;

  // MUST fail: compute_storage_nbytes requires ExternalTensorMeta,
  // not a raw TensorMeta reference.
  auto bytes = crucible::compute_storage_nbytes(meta);
  (void)bytes;
  return 0;
}
