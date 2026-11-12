// A Bounds carries the annotation no_start_over_bytes, so decode cannot make
// a range with lo > hi from the bytes of a word.

#include <foundation/core/Compare.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

int main() {
    std::uint64_t const bytes = 0x0000000000000009u;
    return ::foundation::core::decode<::foundation::core::Bounds<int>>(bytes).is_some() ? 0 : 1;
}
