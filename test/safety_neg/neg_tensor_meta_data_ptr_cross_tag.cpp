// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TensorMeta::data_ptr is ExternalDataPtr, ::fixy::Tagged<void*,
// source::External>.  A pointer under a different provenance tag cannot be
// written into it as if it came from the external tensor-storage boundary.
//
// The pointer here is minted External and retagged Sanitized along the
// admitted edge, so the only defect is the tag at the field write.
//
// Distinct mismatch class from neg_tensor_meta_data_ptr_raw_pointer.cpp:
//   * Companion: a raw void* is refused at the field write.
//   * This fixture: Tagged<void*, Sanitized> is not ExternalDataPtr.

#include <crucible/TensorMeta.h>

int main() {
    auto sanitized = ::fixy::mint_tagged<::fixy::tags::source::External>(static_cast<void*>(nullptr))
                         .retag<::fixy::tags::source::Sanitized>();

    crucible::TensorMeta meta{};

    // MUST fail: Tagged<void*, Sanitized> is not ExternalDataPtr.
    meta.data_ptr = sanitized;
    return 0;
}
