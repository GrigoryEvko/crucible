// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1035 WRAP-TensorMeta-2
// (TensorMeta::data_ptr -> Tagged<void*, source::External>).
//
// Premise: a pointer already carrying a different provenance tag
// cannot be written into TensorMeta::data_ptr as if it originated at
// the external tensor-storage boundary.  Retagging is explicit.
//
// Distinct mismatch class from neg_tensor_meta_data_ptr_raw_pointer.cpp:
//   * Companion: raw void* rejected at the field write surface.
//   * This fixture: Tagged<void*, Sanitized> is not ExternalDataPtr.

#include <crucible/TensorMeta.h>
#include <crucible/safety/Tagged.h>

int main() {
  using SanitizedPtr = crucible::safety::Tagged<
      void*, crucible::safety::source::Sanitized>;

  crucible::TensorMeta meta{};
  SanitizedPtr sanitized{nullptr};

  // MUST fail: Tagged<void*, Sanitized> is not ExternalDataPtr.
  meta.data_ptr = sanitized;
  return 0;
}
