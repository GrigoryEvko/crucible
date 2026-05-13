// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-StorageNbytes-3 fixture #2: the DetSafe tier is part of the
// type.  A Pure storage-byte projection cannot be silently assigned
// to a differently pinned DetSafe slot.

#include <crucible/ir001/MerkleDag.h>

#include <cstdint>

int main() {
  using Raw = crucible::safety::Saturated<std::uint64_t>;
  using ClockReadBytes = crucible::safety::DetSafe<
      crucible::safety::DetSafeTier_v::MonotonicClockRead, Raw>;

  crucible::TensorMeta meta{};
  meta.ndim = 0;
  meta.dtype = crucible::ScalarType::Float;

  ClockReadBytes bytes = crucible::compute_storage_nbytes_det(
      crucible::external_tensor_meta(meta));
  (void)bytes;
  return 0;
}
