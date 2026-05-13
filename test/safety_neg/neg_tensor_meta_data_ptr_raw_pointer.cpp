// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1035 WRAP-TensorMeta-2
// (TensorMeta::data_ptr -> Tagged<void*, source::External>).
//
// Premise: TensorMeta::data_ptr is a foreign tensor-storage address
// observed through PyTorch / trace / disk boundaries.  A raw pointer
// must not be assignable into the field without explicitly crossing
// the External provenance gate with external_data_ptr(ptr).
//
// Distinct mismatch class from neg_tensor_meta_data_ptr_cross_tag.cpp:
//   * This fixture: raw void* rejected at the field write surface.
//   * Companion: wrong Tagged provenance rejected at the same surface.

#include <crucible/TensorMeta.h>

int main() {
  crucible::TensorMeta meta{};
  void* raw = nullptr;

  // MUST fail: data_ptr is ExternalDataPtr, not a raw void*.
  meta.data_ptr = raw;
  return 0;
}
