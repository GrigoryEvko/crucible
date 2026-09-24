// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// compute_storage_nbytes takes ExternalTensorMeta, ::fixy::Tagged<const
// TensorMeta&, source::External>.  A TensorMeta under a different
// provenance tag cannot be passed as if it were External.  Two Tagged
// instantiations with different tags do not convert into each other.
//
// The view here is minted External and retagged Sanitized along the
// admitted edge, so the only defect is the tag at the call.
//
// Distinct mismatch class from neg_storage_nbytes_raw_tensor_meta.cpp:
//   * Companion: a raw TensorMeta is refused at the call.
//   * This fixture: a view under another tag is refused at the call.

#include <crucible/MerkleDag.h>

int main() {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(8);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;

    auto sanitized = ::fixy::mint_tagged<::fixy::tags::source::External, const crucible::TensorMeta&>(meta)
                         .retag<::fixy::tags::source::Sanitized>();

    // MUST fail: Tagged<const TensorMeta&, Sanitized> is not
    // ExternalTensorMeta.
    auto bytes = crucible::compute_storage_nbytes(sanitized);
    (void)bytes;
    return 0;
}
