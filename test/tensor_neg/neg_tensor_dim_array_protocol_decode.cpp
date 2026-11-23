// Negative-compile test. This file must fail to compile.
//
// decode of the Protocol family builds a TensorDimArray from 64 bytes of
// outside, with no check of the bound of a lane.  A lane of the block then
// converts to a TensorDim past the bound.  The block carries the annotation
// no_start_over_bytes, so it is not Decodable.  Lanes from outside go
// through decode_tensor_meta.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <cstddef>

int main() {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::TensorDimArray>> image{};
    return ::fixy::decode<crucible::TensorDimArray>(image.view()).is_ok() ? 0 : 1;
}
