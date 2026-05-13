// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-StorageNbytes-3 fixture #1: compute_storage_nbytes_det()
// returns DetSafe<Pure, Saturated<uint64_t>>, not a raw Saturated
// value.  The caller must explicitly unwrap at the boundary that
// owns the determinism claim.

#include <crucible/ir001/MerkleDag.h>

#include <cstdint>

int main() {
  crucible::TensorMeta meta{};
  meta.ndim = 0;
  meta.dtype = crucible::ScalarType::Float;

  crucible::safety::Saturated<std::uint64_t> raw =
      crucible::compute_storage_nbytes_det(
          crucible::external_tensor_meta(meta));
  (void)raw;
  return 0;
}
