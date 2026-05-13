// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1022 WRAP-StorageNbytes-5
// (compute_storage_nbytes* TensorMeta input -> ExternalTensorMeta).
//
// Premise: a TensorMeta already carrying a different provenance tag
// cannot be passed to the storage-span boundary as if it were
// source::External.  The caller must choose the correct trust lane
// explicitly; unrelated Tagged instantiations do not implicitly
// convert.
//
// Distinct mismatch class from neg_storage_nbytes_raw_tensor_meta.cpp:
//   * Companion: raw TensorMeta rejected at the write boundary.
//   * This fixture: cross-tag passback rejected at the read boundary.

#include <crucible/MerkleDag.h>
#include <crucible/safety/Tagged.h>

int main() {
  crucible::TensorMeta meta{};
  meta.ndim = 1;
  meta.sizes[0] = ::crucible::tensor_dim(8);
  meta.strides[0] = ::crucible::tensor_dim(1);
  meta.dtype = crucible::ScalarType::Float;

  using SanitizedTensorMeta = crucible::safety::Tagged<
      const crucible::TensorMeta&, crucible::safety::source::Sanitized>;
  SanitizedTensorMeta sanitized{meta};

  // MUST fail: Tagged<const TensorMeta&, Sanitized> is not
  // ExternalTensorMeta.
  auto bytes = crucible::compute_storage_nbytes(sanitized);
  (void)bytes;
  return 0;
}
