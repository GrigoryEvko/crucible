// Negative-compile test. This file must fail to compile.
//
// Violation: the decode of the Protocol family makes a descriptor from the
// bytes of its image, with no check of a claim of the type.  The descriptor
// carries no_start_over_bytes, so it is not Decodable.  The image of the
// store goes through UncheckedTensorMeta and decode_tensor_meta.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>

#include <cstddef>

int main() {
    ::fixy::FixedArray<std::byte, ::fixy::encoded_size_v<crucible::TensorMeta>> image{};
    return ::fixy::decode<crucible::TensorMeta>(image.view()).is_ok() ? 0 : 1;
}
