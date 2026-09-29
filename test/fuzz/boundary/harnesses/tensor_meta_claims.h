#pragma once

// The claims of a TensorMeta that crossed a boundary.  A loader that accepts
// a tensor descriptor gives one whose rank fits the size and stride arrays
// and whose enum fields each name an enumerator.

#include "../harness.h"

#include <crucible/TensorMeta.h>

namespace crucible::fuzz::boundary {

inline void claim_meta_in_range(const char* harness, const TensorMeta& meta) {
    CRUCIBLE_FUZZ_CLAIM(harness, meta.ndim <= kMaxTensorNDim);
    CRUCIBLE_FUZZ_CLAIM(harness, valid_scalar_type(static_cast<std::int8_t>(meta.dtype)));
    CRUCIBLE_FUZZ_CLAIM(harness, valid_device_type(static_cast<std::int8_t>(meta.device_type)));
    CRUCIBLE_FUZZ_CLAIM(harness, valid_layout(static_cast<std::int8_t>(meta.layout)));
}

}  // namespace crucible::fuzz::boundary
