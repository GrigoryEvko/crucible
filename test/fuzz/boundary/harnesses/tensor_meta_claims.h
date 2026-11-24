#pragma once

// The claims of a TensorMeta that crossed a boundary.  A loader that accepts
// a tensor descriptor gives one whose rank fits the size and stride arrays
// and whose enum fields each name an enumerator.

#include "../harness.h"

#include <crucible/TensorMeta.h>

#include <cstdint>

namespace crucible::fuzz::boundary {

inline void claim_meta_in_range(HarnessName harness, const TensorMeta& meta) noexcept {
    CRUCIBLE_FUZZ_CLAIM(harness, meta.ndim <= kMaxTensorNDim);
    CRUCIBLE_FUZZ_CLAIM(harness, ::fixy::enum_from<ScalarType>(static_cast<std::int8_t>(meta.dtype)).is_some());
    CRUCIBLE_FUZZ_CLAIM(harness, ::fixy::enum_from<DeviceType>(static_cast<std::int8_t>(meta.device_type)).is_some());
    CRUCIBLE_FUZZ_CLAIM(harness, ::fixy::enum_from<Layout>(static_cast<std::int8_t>(meta.layout)).is_some());
}

}  // namespace crucible::fuzz::boundary
