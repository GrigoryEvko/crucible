// The bit copy door of the Scalar family refuses a sum as its destination.
// The slot of a sum holds a union and carries the mark closed_tag, and a
// OneOf carries no_start_over_bytes, so a OneOf is not BitValid.

#include <foundation/core/OneOf.h>
#include <foundation/core/Scalar.h>

#include <cstdint>

struct ForgedReading {
    std::uint32_t storage = 0;
    std::uint32_t tag_word = 200;
};

using Reading = foundation::core::OneOf<std::uint32_t, std::uint16_t>;

int main() { return static_cast<int>(foundation::core::bit_cast<Reading>(ForgedReading{}).which().raw()); }
